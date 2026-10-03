# 测试 URL 收藏夹

自测用的下载目标 + 一键脚本。所有脚本从本文件夹直接运行，输出统一存到 `test_urls/downloads/`。

## 脚本一览

| 脚本 | 测什么 | URL |
|------|--------|-----|
| `dl_dummy.bat` | 小文件 + 支持 Range → 多线程下载 | W3C 官方测试 PDF（13KB，稳定） |
| `dl_big.bat` | 大文件 → 多线程真加速 + 中断续传 | OVH 测速文件 `10Mb.dat`（10MB，稳定） |
| `dl_local.bat` | 本地服务器 → 离线可演示 + 单线程分支 | `http://127.0.0.1:8000/test.pdf` |
| `probe_url.bat <URL>` | 探测任意 URL 是否支持 Range | 自己输入 |

## 怎么用

```powershell
cd "E:\Vs code\code\多线程文件下载及断点续传实现"
.\test_urls\dl_dummy.bat        # 小文件多线程演示
.\test_urls\dl_big.bat          # 跑几秒按 Ctrl+C，再跑一次 = 断点续传演示
.\test_urls\dl_local.bat        # 离线演示（含"不支持 Range"分支）
.\test_urls\probe_url.bat "https://example.com/file.zip"
```

## 断点续传的标准表演流程

1. 运行 `dl_big.bat`，进度条到 30% 左右按 `Ctrl+C`
2. 查看 `test_urls\downloads\10Mb.dat.meta`——每个分片下载到哪个字节都记着
3. 再次运行 `dl_big.bat`——控制台打印“检测到断点记录，继续上次下载”和“断点续传，从 offset X 继续”
4. 下完后 `.meta` 自动删除，`downloads\10Mb.dat` 是完整文件

## 关于“不支持 Range”的分支

用 `dl_local.bat` 演示：Python 自带的 `http.server` 不发送 `Accept-Ranges` 头，probe 会显示“否”，程序自动走单线程 `downloadFull` 兜底——两条分支都能覆盖，且完全离线可用。

> 注意：`httpbin.org` 这类公共服务时好时坏（有时谎报支持 Range 却忽略 Range 头），不要拿它做演示。程序遇到这种情况会做字节数校验、主动报“下载字节数不符”而拒绝写坏文件——这是保护机制，但演示效果不好。

## 本地测试（dl_local.bat）

先开一个终端：

```powershell
# 在任意有测试文件的目录（放个 test.pdf 进去）
python -m http.server 8000
```

再运行 `dl_local.bat`。

## 自己攒 URL 的判断标准

拿到新 URL 先探测：

```powershell
.\build\downloader.exe probe "<URL>"
```

`支持断点续传: 是` → 多线程 + 断点续传全功能；`否` → 单线程兜底。任何官网安装包（VS Code、Node.js 等）和国内镜像站（清华 TUNA、阿里云、中科大）的资源都支持 Range。
