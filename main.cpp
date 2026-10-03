#include "http_client.h"
#include "download_manager.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// Windows 控制台默认用 GBK/ACP 代码页，中文输出会乱码
// 把输出和输入都切成 UTF-8，保证日志和进度条显示正常
void setupConsoleUtf8()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

void printUsage()
{
    std::cout << "用法:\n";
    std::cout << "  downloader probe <url>\n";
    std::cout << "  downloader download <url> <save_path> [thread_count]\n";
    std::cout << "\n";
    std::cout << "示例:\n";
    std::cout << "  downloader probe \"https://example.com/file.zip\"\n";
    std::cout << "  downloader download \"https://example.com/file.zip\" \"out.zip\" 4\n";
}

// probe 子命令：只发 HEAD 请求，打印文件信息，不下载
int cmdProbe(const std::string &url)
{
    FileInfo info;
    std::string error;

    if (!HttpClient::probe(url, info, error))
    {
        std::cout << "探测失败: " << error << "\n";
        return 1;
    }

    std::cout << "=== 远程文件信息 ===\n";
    std::cout << "URL:           " << url << "\n";
    std::cout << "文件名:        " << info.filename << "\n";
    std::cout << "文件大小:      " << info.total_size << " bytes\n";
    std::cout << "支持断点续传:  " << (info.supports_range ? "是" : "否") << "\n";
    return 0;
}

// download 子命令：探测 → 分片 → 多线程下载 → 写 .meta
int cmdDownload(const std::string &url, const std::string &save_path, int thread_count)
{
    DownloadConfig config;
    config.url = url;
    config.save_path = save_path;
    config.thread_count = thread_count;

    std::string error;
    if (!DownloadManager::download(config, error))
    {
        std::cout << "下载失败: " << error << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setupConsoleUtf8();

    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "probe")
    {
        if (argc != 3)
        {
            std::cout << "错误: probe 需要一个 URL 参数\n\n";
            printUsage();
            return 1;
        }
        return cmdProbe(argv[2]);
    }

    if (command == "download")
    {
        if (argc < 4 || argc > 5)
        {
            std::cout << "错误: download 需要 <url> <save_path> [thread_count]\n\n";
            printUsage();
            return 1;
        }

        std::string url = argv[2];
        std::string save_path = argv[3];
        int thread_count = 4; // 默认 4 线程
        if (argc == 5)
        {
            thread_count = std::stoi(argv[4]);
            if (thread_count < 1)
            {
                std::cout << "错误: 线程数必须 >= 1\n";
                return 1;
            }
        }
        return cmdDownload(url, save_path, thread_count);
    }

    std::cout << "错误: 未知命令 \"" << command << "\"\n\n";
    printUsage();
    return 1;
}