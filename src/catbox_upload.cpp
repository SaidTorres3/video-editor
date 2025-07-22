#include "catbox_upload.h"
#include "options_window.h"
#include <curl/curl.h>
#include <commctrl.h>
#include <string>

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

static std::wstring Trim(const std::wstring& in)
{
    size_t start = 0;
    while (start < in.size() && iswspace(in[start]))
        ++start;
    size_t end = in.size();
    while (end > start && iswspace(in[end - 1]))
        --end;
    return in.substr(start, end - start);
}

static std::string Narrow(const std::wstring& w) {
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
 nullptr);
    std::string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

bool UploadToCatbox(const std::wstring& filePath, std::string& outUrl, HWND progressBar) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_mime* mime = curl_mime_init(curl);
    if (!mime) { curl_easy_cleanup(curl); return false; }

    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "reqtype");
    curl_mime_data(part, "fileupload", CURL_ZERO_TERMINATED);

    std::wstring trimmedHash = Trim(g_catboxUserHash);
    if (!trimmedHash.empty()) {
        part = curl_mime_addpart(mime);
        curl_mime_name(part, "userhash");
        std::string hash = Narrow(trimmedHash);
        curl_mime_data(part, hash.c_str(), CURL_ZERO_TERMINATED);
    }
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "fileToUpload");
    std::string path = Narrow(filePath);
    curl_mime_filedata(part, path.c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://catbox.moe/user/api.php");
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCB);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (progressBar) {
        SendMessage(progressBar, PBM_SETPOS, 0, 0);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCB);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progressBar);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    long httpCode = 0;
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_mime_free(mime);
    if (res != CURLE_OK || httpCode != 200) {
        curl_easy_cleanup(curl);
        return false;
    }

    // Trim whitespace if present
    while (!response.empty() &&
           (response.back() == '\n' || response.back() == '\r' || response.back() == ' '))
        response.pop_back();
    outUrl = response;
    curl_easy_cleanup(curl);
    return !outUrl.empty() && outUrl.rfind("http", 0) == 0;
}
