# TTPlayer 重建版音乐窗 / ttpbrowser.dll

实现与验证日期：2026-10-22

## 用途与部署

ttpbrowser.dll 是 Win32/x86 可选组件，与 TTPlayerRebuild.exe 放在同一目录。
主程序使用绝对路径 LoadLibraryEx + ttp_browser_get_api(1)，不静态链接该 DLL。
DLL 缺失、位数错误、导出缺失或 ABI 不兼容时，不显示音乐窗按钮、菜单、
快捷键设置行、播放列表下载入口及下载设置页；陈旧命令和热键也不会打开窗口。
DLL 按进程启动时检测，添加或移除后需要重启播放器。

默认首页恢复为 http://mini.qianqian.com/index.html。
2026-10-22 实测 HTTP 响应为 302，Location 是
http://c.taihe.com/res/download_pcclient/index.html。
原始响应保留在 ../../reverse/music-window-5790/portal-http-20261022.txt。
这个地址当前是客户端下载安装入口，不能据此声称当年的搜索、榜单、推荐
服务器已经恢复。当前实现恢复本地宿主和脚本协议，不重建未取得的远端数据服务。

## 新系统使用 WebView2 的结论

可用。当前版本将 x86 WebView2LoaderStatic.lib 静态链接到 ttpbrowser.dll，
在 64 位 Windows 10/11 上调用已安装的 WebView2 Runtime 运行 Chromium。
安装只需部署 ttpbrowser.dll；Loader 与 Runtime 是不同组件，合并 Loader 不等于安装 Runtime。

默认策略是 WebView2 优先，Runtime 不可用或初始化失败时使用 IE/MSHTML。
Windows XP 没有 WebView2 支持，保留 IE 控件路线。Windows 7/8/8.1 的 WebView2
停留在 109，不应把现代 Evergreen 当成这些系统的可维护方案。本次已对主程序与
DLL执行 XP/Win7 静态导入审计；当前主机上的 IE 测试不等同于旧系统 VM 实测。

重建配置 Player 节新增属性：

```text
BrowserEngine="0"    自动：WebView2 -> IE（默认）
BrowserEngine="1"    强制 IE/MSHTML
BrowserEngine="2"    必须 WebView2；缺 Runtime 时在窗内显示错误和重试
BrowserHomeURL="http://mini.qianqian.com/index.html"
BrowserAutoStartup="1"  启动时显示；可以通过音乐窗底部复选框修改
BrowserVisible="..."、BrowserWnd="..."、BrowserWnd2="..." 保存窗口状态。
```

这些属性在程序退出时保存，手动编辑请先关闭播放器。旧配置缺少新属性时使用默认值。

## WebView2 与原版的适配点

原版通过 IE 的 IDispatch/window.external 调用本地主程序。WebView2 提供
AddHostObjectToScript，原生位置是 chrome.webview.hostObjects，而非旧接口。
DLL 在文档创建前安装兼容脚本，将 window.external.ttplayer 至 ttplayer6
映射到同步 COM 代理。查询可同步返回；播放列表选择、加歌、下载、更新等操作
排入宿主 UI 消息队列，避免在 WebView2 COM 回调内启动模态消息循环。

脚本桥仅授予配置首页的精确 origin（协议、主机、端口）且窗口当前可见的音乐页。
跨域跳转可以浏览，但不会获得播放器接口。WebView2 的桥仅安装到顶层文档；
IE 无法在 GetExternal 精确区分帧，因此另外拦截不受信任的子帧导航。
导航/关闭会撤销旧文档未提交的歌曲批次及尚未处理的文档操作。
后台下载仍会继续；关闭整个播放器时取消任务、等待 worker，再释放宿主状态。

## 窗口、下载与主程序的职责

DLL：TTPlayer_BrowserWnd、浏览器后端、前进/后退/刷新、音乐/下载标签、链接、
启动复选框、皮肤按钮/字体/透明色/九宫格缩放、错误面板/重试、脚本 IDispatch、
推荐歌曲菜单、原版资源下载列表、队列、worker、并发控制、暂停/开始/移除、任务持久化。
下载列表持久化位置沿用设置目录下 HttpDownloads/cachelist.xml；旧缓存列表导入保留为内部迁移接口。
移除任务/删除记录保留已下载文件；“删除文件”需在界面确认。接收推荐歌曲服从原 Network 的相应设置。
下载页沿用原版顶部设置、80px分类栏、自绘表头/进度和右键菜单；没有额外底部工具栏。
音乐窗去掉系统缩放框，使用皮肤窗口区域及客户区拖动/八方向缩放。修复依据与边界见 [UI_FIX.zh-CN.md](UI_FIX.zh-CN.md)。
背景按九宫格拉伸或平铺；默认皮肤缺少 `resize_rect` 时使用中心像素带，保留边角尺寸，详见 [NINE_PATCH_FIX.zh-CN.md](NINE_PATCH_FIX.zh-CN.md)。
音乐窗通过宿主回调共用吸附开关、距离与面板登记表，支持工作区及其他面板的移动/缩放磁吸；主窗可带动相连音乐窗。此功能需同时更新 EXE 和 DLL，详见 [SNAP_FIX.zh-CN.md](SNAP_FIX.zh-CN.md)。

EXE：可选 DLL 加载、皮肤参数快照、窗口状态保存、现有播放列表/播放引擎、
现有 HTTP IStream 生产者和网络配置。DLL 通过 open_stream 回调消费 IStream，
这样播放与下载继续共用已有代理/TLS/缓存传输实现，不复制一套网络协议栈。
EXE 中的 legacy_cache.cpp 只保留缓存索引等辅助逻辑，下载队列不再属于 EXE。
Tid 写入曲目 metadata 的 TTPlayer.Tid 键，后续读信息/刷新保留该标识。

## 恢复的脚本接口

外层：ttplayer/ttplayer2/ttplayer3/ttplayer4/ttplayer5/ttplayer6 属性、
AddRecomdSound(source, artist, title)、ClearRecomdList()。
内层 15 项：

```text
AddSoundToPlay、AddSoundToList、AddBrowserHyper、ClearBrowserHyper、DownSetupPack、
BeginAddSongs、AddSong、EndAddSongs、GetSkinInfo、GetCacheInfo、GetPlayerInfo、
AddSoundToPlayByTid、AddSoundToListByTid、AddDownloadInfo、AddDownloadInfoByTid。
```

AddSound/AddDownload 普通形式为 source, artist, title, commit；ByTid 形式为
source, artist, title, tid, commit，Tid 为字符串，不是下载 URL 的替代参数。
Begin 清空暂存；AddSong 暂存；EndAddSongs(mode0,mode1,mode2) 一次提交。
GetPlayerInfo(1) 返回兼容版本 5.7.9，(2) 返回 0；GetCacheInfo 保留原 Invoke E_FAIL。

## 明确的重建设计与边界

推荐歌曲显示在 DLL 的独立菜单中，由用户选取后提交播放；未声称逐字节复制原版内部列表。
mode0 选择目标播放列表，mode1 映射当前插入上下文/末尾追加，mode2 控制播放。
原生播放器内部所有列表分支尚未逐一动态复现，依据见 HOST_IMPLEMENTATION.txt。
AddBrowserHyper 对 URL/标题参数提供双顺序容错，这是重建策略，不是原版版本差异证据。
DownSetupPack 进入现有受控更新流程，不执行网页指定的任意程序、URL 或命令行。
本地桥传入 HTTP(S) 媒体源；不能凭 Tid 自动恢复未取得的旧服务器解析协议。

## ABI 与生命周期

include/ttpbrowser.h 定义 size/version 验证的 C ABI；字符串、HBITMAP 在调用中复制。
除 host.open_stream（worker）外，API 和宿主回调在创建实例的 UI 线程使用。
主机为下载流复制取消 HANDLE，流不持有 DLL 内闭包；退出时先断开 UI 回调，再取消/join。
WebView2 异步 COM 回调可能晚于 Close，首次启动后将 DLL 模块代码保持到进程退出；
实例、窗口、宿主回调仍正常释放。这保证异步回调不会跳入已卸载代码。
诊断 external_dispatch 返回 AddRef 对象；destroy 后对象只返回断开错误，调用者必须
在 FreeLibrary 前 Release 全部对象并保留 DLL 加载引用。

## 源码入口

```text
include/ttpbrowser.h             公开 ABI
src/module.cpp                  导出、实例生命周期
src/browser_window.cpp          原生音乐窗、皮肤、状态与操作分发
src/browser_bridge.cpp          IDispatch 方法与参数契约
src/webview_backend.cpp         WebView2、旧接口兼容脚本
src/ie_backend.cpp              IE/MSHTML + ATL 后备宿主
src/downloads.cpp               下载子视图与配置
src/downloads_ui.inc            原版布局、绘制与下载交互
src/download_skin.cpp           从宿主资源读取原版下载皮肤
src/http_download_queue.cpp     队列、持久化、下载/预取 worker
../../rebuild/src/ui/browser_client.cpp       EXE 可选加载与流回调
../../rebuild/src/ui/player_window_browser.cpp 主程序播放/皮肤/状态接入
```

## 构建与测试

要求 MSVC Win32/x86、Windows SDK、Microsoft.Web.WebView2 SDK 1.0.3179.45。
可通过 `-DTTPBROWSER_WEBVIEW2_SDK=<解包目录>` 指定 SDK；未指定时自动下载固定版本并校验 SHA-256。
主项目沿用已有 VC-LTL/YY-Thunks 配置；无需把 WebView2 静态导入到 EXE 或 DLL。

在工作区根目录执行：

```text
cmake -S rebuild -B rebuild/build -DTTPLAYER_BUILD_BROWSER=ON -DTTPBROWSER_BUILD_TESTS=ON
cmake --build rebuild/build --config Release --target ttplayer_rebuild ttpbrowser_contract_tests ttpbrowser_runtime_tests ttpbrowser_download_tests ttpbrowser_host_tests -j 4
ctest --test-dir rebuild/build/ttpbrowser -C Release --output-on-failure
python ttpbrowser/tests/run_runtime_tests.py rebuild/build/ttpbrowser/Release --output ttpbrowser/out/runtime-check
python ttpbrowser/tests/run_host_tests.py rebuild/build --output ttpbrowser/out/host-check-new
```

集成构建后 EXE 目录只复制 ttpbrowser.dll，WebView2 Loader 已静态合并。
也可在 ttpbrowser 目录运行 `./build.ps1 -Package` 独立构建和打包，无需旁边的 rebuild 目录。
构建与 Action 说明见 [BUILD.md](BUILD.md)；真实主程序宿主隔离测试仍依赖完整 rebuild。
runtime_tests 使用本机 loopback HTTP fixture，不访问历史音乐服务。tests/host_tests.cpp 只在
带 .host-probe-isolated 标记的隔离运行目录执行，检查 DLL 存在/缺失时的真实 UI 状态。
测试结果与二进制校验记录见 [VALIDATION.zh-CN.md](VALIDATION.zh-CN.md)。

## 官方依据

[https://learn.microsoft.com/en-us/microsoft-edge/webview2/](https://learn.microsoft.com/en-us/microsoft-edge/webview2/)
[https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/hostobject](https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/hostobject)
[https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)
[https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)
[https://blogs.windows.com/msedgedev/2022/12/09/microsoft-edge-and-webview2-ending-support-for-windows-7-and-windows-8-8-1/](https://blogs.windows.com/msedgedev/2022/12/09/microsoft-edge-and-webview2-ending-support-for-windows-7-and-windows-8-8-1/)

原版静态证据与恢复边界见 ../../reverse/music-window-5790/ANALYSIS.zh-CN.txt、
bridge-interface.json、code/、HOST_IMPLEMENTATION.txt，未运行或改写原版安装目录。
