# TTPlayer 音乐窗与下载管理

`ttpbrowser.dll` 是重建版播放器的可选 x86 组件，提供原生皮肤音乐窗、网页脚本桥和下载管理。

关闭播放器，将 `ttpbrowser.dll` 放在 `TTPlayerRebuild.exe` 同一目录，重新启动即可。此组件不放入 `AddIn`。DLL 缺失或不兼容时，主程序隐藏音乐窗及下载相关入口。

WebView2 Loader 已静态合并到组件，默认使用 WebView2，无法使用时回退 IE/MSHTML。浏览器 Runtime 仍需安装，安装要求见 [Microsoft 分发说明](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution)。原 `mini.qianqian.com` 入口保留，远端搜索、榜单等服务不由本组件恢复。

## 构建与发行

在本目录执行：

```powershell
./build.ps1 -Package
# 与发行标签一致的日期版本：
./build.ps1 -Package -PackageVersion 2026.10.22p1
```

需要 Windows、MSVC x86/ATL 工具、Windows SDK、CMake 3.24+、Python 3 和 PowerShell。默认生成器为 Visual Studio 2026；可用 `-Generator` 指定其它兼容生成器。独立构建无需相邻 `rebuild` 目录或本机 NuGet 缓存。

GitHub Actions 与其它插件保持相同用法：手动运行 **Build TTPlayer music window**，勾选 **Release a Version (GitHub)** 才会发布。版本采用北京时间 `yyyy.MM.dd`，同日后续发行使用 `p1`、`p2`，编译前写入 DLL 的文件和产品版本。

输出 `build/Release/ttpbrowser-<版本>.zip` 与外部 `SHA256SUMS.txt`。ZIP 包含一个 DLL、第三方声明及包内校验和，不包含测试、样本或本地分析产物。Action 默认只构建、审计和打包；测试可在本地显式开启。

## 文档

- [实现、配置与脚本接口](docs/README.zh-CN.md)
- [独立构建、Actions 和依赖](docs/BUILD.md)
- [本次构建与打包验证](docs/BUILD_VALIDATION.md)
- [首次交付验证记录](docs/VALIDATION.zh-CN.md)
- [启动闪退修复](docs/STARTUP_FIX.zh-CN.md)
- [外围绘制与下载界面修复](docs/UI_FIX.zh-CN.md)
- [音乐窗吸附分析与修复](docs/SNAP_FIX.zh-CN.md)
- [第三方声明](docs/licenses/)
