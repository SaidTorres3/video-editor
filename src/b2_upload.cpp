#include "b2_upload.h"
#include "options_window.h"
#include <curl/curl.h>
#include <string>
#include <commctrl.h>

static size_t WriteCB(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

static int ProgressCB(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t ultotal, curl_off_t ulnow) {
    HWND bar = reinterpret_cast<HWND>(clientp);
    if (bar && ultotal > 0) {
        int pct = static_cast<int>((double)ulnow / ultotal * 100.0);
        SendMessage(bar, PBM_SETPOS, pct, 0);
    }
    return 0;
}

static std::string Narrow(const std::wstring& w) {
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

static bool ExtractJson(const std::string& json, const std::string& key, std::string& value) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return false;
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return false;
    value = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool UploadToB2(const std::wstring& filePath, std::string& outUrl, HWND progressBar) {
    if (g_b2KeyId.empty() || g_b2AppKey.empty() || g_b2BucketId.empty() || g_b2BucketName.empty())
        return false;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.backblazeb2.com/b2api/v2/b2_authorize_account");
    std::string creds = Narrow(g_b2KeyId) + ":" + Narrow(g_b2AppKey);
    curl_easy_setopt(curl, CURLOPT_USERPWD, creds.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) { curl_easy_cleanup(curl); return false; }

    std::string authToken, apiUrl, downloadUrl;
    if (!ExtractJson(response, "authorizationToken", authToken) ||
        !ExtractJson(response, "apiUrl", apiUrl) ||
        !ExtractJson(response, "downloadUrl", downloadUrl)) {
        curl_easy_cleanup(curl);
        return false;
    }

    response.clear();
    struct curl_slist* hdrs = nullptr;
    std::string authHeader = "Authorization: " + authToken;
    hdrs = curl_slist_append(hdrs, authHeader.c_str());
    std::string postData = std::string("{\"bucketId\":\"") + Narrow(g_b2BucketId) + "\"}";
    curl_easy_setopt(curl, CURLOPT_URL, (apiUrl + "/b2api/v2/b2_get_upload_url").c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    if (res != CURLE_OK) { curl_easy_cleanup(curl); return false; }

    std::string uploadUrl, uploadAuth;
    if (!ExtractJson(response, "uploadUrl", uploadUrl) ||
        !ExtractJson(response, "authorizationToken", uploadAuth)) {
        curl_easy_cleanup(curl);
        return false;
    }

    FILE* fp = _wfopen(filePath.c_str(), L"rb");
    if (!fp) { curl_easy_cleanup(curl); return false; }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::wstring wname = filePath.substr(filePath.find_last_of(L"/\\") + 1);
    std::string name = Narrow(wname);
    char* esc = curl_easy_escape(curl, name.c_str(), 0);

    hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, ("Authorization: " + uploadAuth).c_str());
    hdrs = curl_slist_append(hdrs, (std::string("X-Bz-File-Name: ") + esc).c_str());
    hdrs = curl_slist_append(hdrs, "Content-Type: b2/x-auto");
    hdrs = curl_slist_append(hdrs, "X-Bz-Content-Sha1: do_not_verify");

    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fsz);
    if (progressBar) {
        SendMessage(progressBar, PBM_SETPOS, 0, 0);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCB);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progressBar);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }
    response.clear();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_free(esc);
    fclose(fp);
    if (res != CURLE_OK) { curl_easy_cleanup(curl); return false; }

    if (!g_b2CustomUrl.empty()) {
        std::string base = Narrow(g_b2CustomUrl);
        if (base.back() != '/' && base.back() != '\\') base += '/';
        outUrl = base + name;
    } else {
        outUrl = downloadUrl + "/file/" + Narrow(g_b2BucketName) + "/" + name;
    }
    curl_easy_cleanup(curl);
    return true;
}
