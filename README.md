# TTPlayer 音乐窗与下载管理

`ttpbrowser.dll` 是重建版播放器的可选 x86 组件，提供原生皮肤音乐窗、网页脚本桥和下载管理。

支持 **OpenSubsonic** 与 **OpenList** 个人媒体库：在主程序选项的「网络连接 → 音乐窗 → 媒体来源」配置来源、浏览或搜索歌曲，并交给千千播放、加入列表或下载。`2026.10.28p7` 使用 DLL 内置的 WebView2 网页界面，提供悬停封面播放、紧凑歌曲列表和底部分页/操作栏；成功操作后不再显示底部状态文字，失败提示位于列表内。右下角齿轮菜单提供设置入口和来源切换。选项页以名称/类型列表管理来源，支持添加、删除、编辑、排序和多个同类型服务。来源列表按歌词服务器编辑器使用原生对话框与缓冲绘制。个人媒体库请求优先使用 `AddIn/ttp_https.dll` ABI 8，组件缺失、旧版或明确要求系统代理处理时回退 WinINet。音乐窗显示/关闭遵循主程序淡入淡出设置，边框菜单复用主程序菜单样式并提供“选项”入口；启动时恢复上次显示/关闭状态，新配置默认关闭；本次需配套更新 2026.10.28p2 主程序。配置及本机测试说明见 [个人媒体库](docs/PERSONAL_MEDIA.zh-CN.md)。

关闭播放器，将 `ttpbrowser.dll` 放在 `TTPlayerRebuild.exe` 同一目录，重新启动即可。此组件不放入 `AddIn`。DLL 缺失或不兼容时，主程序隐藏音乐窗及下载相关入口。

`2026.10.28p9` 配合主程序 `2026.10.28p5`、`ttp_osu 2026.10.28p2` 增加 **Sayobot osu! 镜像站**。配置的 osu!／lazer 程序至少一个通过插件特征识别时，边框右键和个人媒体库齿轮菜单才显示该入口；路径失效时同步隐藏并退出已打开的镜像站。详情页“播放”把所选难度的完整音频和歌曲信息加入当前播放列表并立即播放；仅接管详情按钮，卡片试听保留网页播放与暂停；详情接管时暂停已有试听，不改变静音或音量。卡片下载及详情“下载／无视频／mini”交给下载管理，按服务器文件名保存 `.osz`，重名按原覆盖设置处理，未启用覆盖时加数字序号。曲包本身不加入播放列表。缺少组件或使用旧组件时不显示。

WebView2 Loader 已静态合并到组件。个人媒体库默认使用 WebView2，无法使用时按 IE → 原生列表回退；历史音乐网页继续保留 WebView2 / IE 策略。浏览器 Runtime 仍需安装，安装要求见 [Microsoft 分发说明](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution)。历史 `mini.qianqian.com` 配置保留用于兼容，边框右键不再提供“音乐网页”选项，远端搜索、榜单等服务不由本组件恢复。

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
- [OpenSubsonic 与 OpenList 个人媒体库](docs/PERSONAL_MEDIA.zh-CN.md)
- [独立构建、Actions 和依赖](docs/BUILD.md)
- [本次构建与打包验证](docs/BUILD_VALIDATION.md)
- [首次交付验证记录](docs/VALIDATION.zh-CN.md)
- [启动闪退修复](docs/STARTUP_FIX.zh-CN.md)
- [外围绘制与下载界面修复](docs/UI_FIX.zh-CN.md)
- [音乐窗吸附分析与修复](docs/SNAP_FIX.zh-CN.md)
- [音乐窗九宫格拉伸修复](docs/NINE_PATCH_FIX.zh-CN.md)
- [第三方声明](docs/licenses/)
