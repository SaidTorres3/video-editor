#include "b2_upload.h"
#include <curl/curl.h>
#include <fstream>
#include <vector>
#include <filesystem>

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    std::string* s = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    s->append(static_cast<char*>(contents), total);
    return total;
}

static std::string Base64Encode(const std::string& in)
{
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val=0, valb=-6;
    for(unsigned char c : in) {
        val = (val<<8) + c;
        valb += 8;
        while(valb >= 0) {
            out.push_back(tbl[(val>>valb)&0x3F]);
            valb -= 6;
        }
    }
    if(valb>-6) out.push_back(tbl[((val<<8)>>(valb+8))&0x3F]);
    while(out.size()%4) out.push_back('=');
    return out;
}

static bool HttpGet(const std::string& url, const std::vector<std::string>& headers, std::string& response)
{
    CURL* curl = curl_easy_init();
    if(!curl) return false;
    struct curl_slist* hdr = nullptr;
    for(const auto& h : headers) hdr = curl_slist_append(hdr, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

static bool HttpPost(const std::string& url, const std::string& body, const std::vector<std::string>& headers, std::string& response)
{
    CURL* curl = curl_easy_init();
    if(!curl) return false;
    struct curl_slist* hdr = nullptr;
    for(const auto& h : headers) hdr = curl_slist_append(hdr, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}

static std::string ExtractJson(const std::string& json, const std::string& key)
{
    std::string k = "\"" + key + "\"";
    size_t pos = json.find(k);
    if(pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if(pos == std::string::npos) return "";
    pos = json.find('"', pos);
    if(pos == std::string::npos) return "";
    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if(end == std::string::npos) return "";
    return json.substr(start, end-start);
}

bool UploadToBackblaze(const std::wstring& filePath, std::wstring& outUrl)
{
    char keyId[256], appKey[256], bucketId[256], bucketName[256];
    if(!GetEnvironmentVariableA("B2_KEY_ID", keyId, sizeof(keyId))) return false;
    if(!GetEnvironmentVariableA("B2_APP_KEY", appKey, sizeof(appKey))) return false;
    if(!GetEnvironmentVariableA("B2_BUCKET_ID", bucketId, sizeof(bucketId))) return false;
    if(!GetEnvironmentVariableA("B2_BUCKET_NAME", bucketName, sizeof(bucketName))) return false;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string auth;
    auth.reserve(strlen(keyId)+strlen(appKey)+1);
    auth.append(keyId).push_back(':');
    auth.append(appKey);
    std::string auth64 = Base64Encode(auth);
    std::string resp;
    std::vector<std::string> hdr = {"Authorization: Basic " + auth64};
    if(!HttpGet("https://api.backblazeb2.com/b2api/v2/b2_authorize_account", hdr, resp)) { curl_global_cleanup(); return false; }
    std::string apiUrl = ExtractJson(resp, "apiUrl");
    std::string authToken = ExtractJson(resp, "authorizationToken");
    std::string downloadUrl = ExtractJson(resp, "downloadUrl");
    if(apiUrl.empty() || authToken.empty() || downloadUrl.empty()) { curl_global_cleanup(); return false; }

    std::string body = std::string("{\"bucketId\":\"") + bucketId + "\"}";
    resp.clear();
    hdr = {"Authorization: " + authToken, "Content-Type: application/json"};
    if(!HttpPost(apiUrl + "/b2api/v2/b2_get_upload_url", body, hdr, resp)) { curl_global_cleanup(); return false; }
    std::string uploadUrl = ExtractJson(resp, "uploadUrl");
    std::string uploadToken = ExtractJson(resp, "authorizationToken");
    if(uploadUrl.empty() || uploadToken.empty()) { curl_global_cleanup(); return false; }

    std::filesystem::path p(filePath);
    std::string filename = p.filename().string();
    std::ifstream fs(filePath, std::ios::binary);
    if(!fs) { curl_global_cleanup(); return false; }
    fs.seekg(0, std::ios::end);
    curl_off_t size = fs.tellg();
    fs.seekg(0, std::ios::beg);

    CURL* curl = curl_easy_init();
    if(!curl) { curl_global_cleanup(); return false; }
    struct curl_slist* uh = nullptr;
    uh = curl_slist_append(uh, ("Authorization: " + uploadToken).c_str());
    uh = curl_slist_append(uh, ("X-Bz-File-Name: " + filename).c_str());
    uh = curl_slist_append(uh, "X-Bz-Content-Sha1: do_not_verify");
    uh = curl_slist_append(uh, "Content-Type: application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_URL, uploadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READDATA, &fs);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, size);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, uh);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(uh);
    curl_easy_cleanup(curl);
    fs.close();
    curl_global_cleanup();
    if(res != CURLE_OK) return false;

    outUrl.assign(std::wstring(downloadUrl.begin(), downloadUrl.end()) + L"/file/" + std::wstring(bucketName, bucketName + strlen(bucketName)) + L"/" + p.filename().wstring());
    return true;
}
