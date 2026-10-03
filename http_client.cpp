#include "http_client.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{

    struct UrlParts
    {
        bool https = false;
        std::wstring host;
        INTERNET_PORT port = 0;
        std::wstring path;
    };

    std::wstring toWide(const std::string &s)
    {
        if (s.empty())
        {
            return L"";
        }
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (len <= 0)
        {
            len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
            if (len <= 0)
            {
                return L"";
            }
            std::wstring out(len - 1, L'\0');
            MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &out[0], len);
            return out;
        }
        std::wstring out(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
        return out;
    }

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim(const std::string &s)
    {
        size_t begin = 0;
        while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])))
        {
            ++begin;
        }
        size_t end = s.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        {
            --end;
        }
        return s.substr(begin, end - begin);
    }

    bool parseUrl(const std::string &url, UrlParts &parts, std::string &error)
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);

        wchar_t host[256] = {};
        wchar_t path[2048] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));

        std::wstring wurl = toWide(url);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        {
            error = "URL 解析失败";
            return false;
        }

        parts.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        parts.host.assign(uc.lpszHostName, uc.dwHostNameLength);
        parts.port = uc.nPort;
        parts.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (parts.path.empty())
        {
            parts.path = L"/";
        }
        return true;
    }

    bool queryHeader(HINTERNET request, DWORD info_level, std::string &value)
    {
        DWORD size = 0;
        WinHttpQueryHeaders(request, info_level, WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
        {
            return false;
        }

        std::wstring buffer(size / sizeof(wchar_t), L'\0');
        if (!WinHttpQueryHeaders(request, info_level, WINHTTP_HEADER_NAME_BY_INDEX,
                                 &buffer[0], &size, WINHTTP_NO_HEADER_INDEX))
        {
            return false;
        }

        while (!buffer.empty() && buffer.back() == L'\0')
        {
            buffer.pop_back();
        }

        int len = WideCharToMultiByte(CP_UTF8, 0, buffer.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
        {
            return false;
        }
        value.assign(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buffer.c_str(), -1, &value[0], len, nullptr, nullptr);
        return true;
    }

    bool sendRequest(HINTERNET request,
                     const std::wstring *extra_headers,
                     std::string &error)
    {
        const wchar_t *headers = extra_headers ? extra_headers->c_str() : WINHTTP_NO_ADDITIONAL_HEADERS;
        DWORD headers_len = extra_headers ? static_cast<DWORD>(-1) : 0;

        if (!WinHttpSendRequest(request, headers, headers_len,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            error = "发送 HTTP 请求失败";
            return false;
        }
        if (!WinHttpReceiveResponse(request, nullptr))
        {
            error = "接收 HTTP 响应失败";
            return false;
        }
        return true;
    }

    std::string filenameFromContentDisposition(const std::string &header)
    {
        const std::string key = "filename=";
        auto pos = header.find(key);
        if (pos == std::string::npos)
        {
            return "";
        }
        std::string name = trim(header.substr(pos + key.size()));
        if (!name.empty() && (name.front() == '"' || name.front() == '\''))
        {
            name.erase(name.begin());
        }
        if (!name.empty() && (name.back() == '"' || name.back() == '\''))
        {
            name.pop_back();
        }
        return name;
    }

    std::string filenameFromUrl(const std::string &url)
    {
        auto pos = url.find('?');
        std::string path = (pos == std::string::npos) ? url : url.substr(0, pos);
        pos = path.rfind('/');
        if (pos == std::string::npos || pos + 1 >= path.size())
        {
            return "download.bin";
        }
        return path.substr(pos + 1);
    }

} // namespace

bool HttpClient::probe(const std::string &url, FileInfo &info, std::string &error)
{
    UrlParts parts;
    if (!parseUrl(url, parts, error))
    {
        return false;
    }

    HINTERNET session = WinHttpOpen(L"MultiThreadDownloader/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        error = "WinHttpOpen 失败";
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect 失败";
        return false;
    }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"HEAD", parts.path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest 失败";
        return false;
    }

    if (!sendRequest(request, nullptr, error))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::string length_text;
    if (queryHeader(request, WINHTTP_QUERY_CONTENT_LENGTH, length_text))
    {
        info.total_size = std::stoll(length_text);
    }

    std::string accept_ranges;
    if (queryHeader(request, WINHTTP_QUERY_ACCEPT_RANGES, accept_ranges))
    {
        info.supports_range = (toLower(trim(accept_ranges)) == "bytes");
    }

    std::string disposition;
    if (queryHeader(request, WINHTTP_QUERY_CONTENT_DISPOSITION, disposition))
    {
        info.filename = filenameFromContentDisposition(disposition);
    }
    if (info.filename.empty())
    {
        info.filename = filenameFromUrl(url);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

int64_t HttpClient::downloadRange(const std::string &url,
                                  int64_t range_start,
                                  int64_t range_end,
                                  std::vector<char> &buffer,
                                  std::string &error)
{
    UrlParts parts;
    if (!parseUrl(url, parts, error))
    {
        return -1;
    }

    HINTERNET session = WinHttpOpen(L"MultiThreadDownloader/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        error = "WinHttpOpen 失败";
        return -1;
    }

    HINTERNET connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect 失败";
        return -1;
    }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", parts.path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest 失败";
        return -1;
    }

    std::ostringstream oss;
    oss << "Range: bytes=" << range_start << "-" << range_end;
    std::string range_header = oss.str();
    std::wstring wheaders = toWide(range_header);

    if (!sendRequest(request, &wheaders, error))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return -1;
    }

    buffer.clear();
    int64_t total = 0;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            error = "查询可用数据失败";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return -1;
        }
        if (available == 0)
        {
            break;
        }

        size_t old_size = buffer.size();
        buffer.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data() + old_size, available, &read))
        {
            error = "读取响应数据失败";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return -1;
        }
        buffer.resize(old_size + read);
        total += read;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return total;
}

int64_t HttpClient::downloadFull(const std::string &url,
                                 std::vector<char> &buffer,
                                 std::string &error)
{
    UrlParts parts;
    if (!parseUrl(url, parts, error))
    {
        return -1;
    }

    HINTERNET session = WinHttpOpen(L"MultiThreadDownloader/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        error = "WinHttpOpen 失败";
        return -1;
    }

    HINTERNET connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
    if (!connect)
    {
        WinHttpCloseHandle(session);
        error = "WinHttpConnect 失败";
        return -1;
    }

    DWORD flags = parts.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", parts.path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request)
    {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = "WinHttpOpenRequest 失败";
        return -1;
    }

    if (!sendRequest(request, nullptr, error))
    {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return -1;
    }

    buffer.clear();
    int64_t total = 0;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            error = "查询可用数据失败";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return -1;
        }
        if (available == 0)
        {
            break;
        }

        size_t old_size = buffer.size();
        buffer.resize(old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data() + old_size, available, &read))
        {
            error = "读取响应数据失败";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return -1;
        }
        buffer.resize(old_size + read);
        total += read;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return total;
}
