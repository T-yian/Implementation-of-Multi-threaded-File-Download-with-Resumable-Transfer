#include "http_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    class CurlHandle
    {
        CURL *h;
    public:
        CurlHandle() : h(curl_easy_init()) {}
        ~CurlHandle() { if (h) curl_easy_cleanup(h); }
        CurlHandle(const CurlHandle &) = delete;
        CurlHandle &operator=(const CurlHandle &) = delete;
        CURL *get() const { return h; }
        bool ok() const { return h != nullptr; }
    };

    size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *buf = static_cast<std::vector<char> *>(userdata);
        size_t total = size * nmemb;
        buf->insert(buf->end(), ptr, ptr + total);
        return total;
    }

    size_t headerCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *headers = static_cast<std::string *>(userdata);
        headers->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim(const std::string &s)
    {
        size_t begin = 0;
        while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
        size_t end = s.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
        return s.substr(begin, end - begin);
    }

    std::string getHeader(const std::string &all_headers, const std::string &name)
    {
        std::string lower_name = toLower(name) + ":";
        std::istringstream iss(all_headers);
        std::string line;
        while (std::getline(iss, line))
        {
            if (toLower(line).rfind(lower_name, 0) == 0)
            {
                return trim(line.substr(name.size() + 1));
            }
        }
        return "";
    }

    std::string filenameFromContentDisposition(const std::string &header)
    {
        const std::string key = "filename=";
        auto pos = toLower(header).find(key);
        if (pos == std::string::npos) return "";
        std::string name = trim(header.substr(pos + key.size()));
        if (!name.empty() && (name.front() == '"' || name.front() == '\'')) name.erase(name.begin());
        if (!name.empty() && (name.back() == '"' || name.back() == '\'')) name.pop_back();
        return name;
    }

    std::string filenameFromUrl(const std::string &url)
    {
        auto pos = url.find('?');
        std::string path = (pos == std::string::npos) ? url : url.substr(0, pos);
        pos = path.rfind('/');
        if (pos == std::string::npos || pos + 1 >= path.size()) return "download.bin";
        return path.substr(pos + 1);
    }

} // namespace

bool HttpClient::probe(const std::string &url, FileInfo &info, std::string &error)
{
    CurlHandle curl;
    if (!curl.ok()) { error = "curl_easy_init 失败"; return false; }

    std::string headers;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) { error = curl_easy_strerror(res); return false; }

    curl_off_t size = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &size);
    info.total_size = static_cast<int64_t>(size);

    info.supports_range = (toLower(getHeader(headers, "Accept-Ranges")) == "bytes");
    info.filename = filenameFromContentDisposition(getHeader(headers, "Content-Disposition"));
    if (info.filename.empty()) info.filename = filenameFromUrl(url);
    return true;
}

int64_t HttpClient::downloadRange(const std::string &url, int64_t range_start, int64_t range_end,
                                  std::vector<char> &buffer, std::string &error)
{
    CurlHandle curl;
    if (!curl.ok()) { error = "curl_easy_init 失败"; return -1; }

    // 手动构造 Range 头（HTTP/2 下 CURLOPT_RANGE 行为不一致，手动头最稳、与 curl 一致）
    std::string range_header = "Range: bytes=" + std::to_string(range_start) + "-" + std::to_string(range_end);
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, range_header.c_str());

    buffer.clear();
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl.get());
    curl_slist_free_all(headers);
    if (res != CURLE_OK) { error = curl_easy_strerror(res); return -1; }
    return static_cast<int64_t>(buffer.size());
}

int64_t HttpClient::downloadFull(const std::string &url, std::vector<char> &buffer, std::string &error)
{
    CurlHandle curl;
    if (!curl.ok()) { error = "curl_easy_init 失败"; return -1; }

    buffer.clear();
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) { error = curl_easy_strerror(res); return -1; }
    return static_cast<int64_t>(buffer.size());
}
