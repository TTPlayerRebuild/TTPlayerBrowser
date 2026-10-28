# 构建与发行验证记录

## IE 设置图标与焦点外观 / 2026-10-28 p12

IE 页面的齿轮改用与 WebView2 设置 SVG 相同的轮廓、描边、颜色和缩放，保持 16×16 图标在 22×22 按钮内居中。覆盖底栏通用按钮的字号与内边距，设置 `hideFocus` 并移除默认轮廓，解决 XP 的系统虚线焦点框；键盘聚焦时用浅色背景提示，鼠标操作清除该提示。设置及来源菜单交互保持可用。

XP IE8、Win7 IE11 的既有真实服务和宿主集成检查各 60 项全部通过，退出码均为 0；检查了正常状态、Esc 返回焦点后的紧凑状态以及来源菜单截图。证据在 `out/ie-settings-20261028p12/{xp,win7}`。

部署 `rebuild/build/Release/ttpbrowser.dll 2026.10.28p12`，759,808 字节，SHA-256 `280c84b28bdf853316da250ee90b500e60874651a25171c04594d2e2f90ccf40`。独立包为 `ttpbrowser-2026.10.28p12.zip`，347,954 字节，包内容及 XP/Win7 静态导入检查通过。部署时真实播放器仍加载 p11，采用移动旧文件并放置新 DLL 的方式更新，没有终止播放器；该实例重启后加载 p12。

## XP IE8 个人媒体库显示与按钮 / 2026-10-28 p11

在用户安装 IE 8.0.6001.18702 的 XP 中确认个人媒体库使用 IE 页面。修复操作按钮换行、设置图标缺字、专辑「打开」分行、窄窗口文字折行及复选框多余边框；图标使用内嵌 PNG，来源菜单兼容 IE8 键盘事件。IE 详情封面超过 data URI 上限时在原生侧生成 96×96 JPEG 缩略图，恢复实际封面显示。

XP IE8 和 Win7 IE11 各通过 8 个真实服务断言及 52 个 UI/宿主/流解码断言，退出码全部为 0，包含两个来源的实际下载及文件名验证。证据为 `out/xp-ie8-20261028/{xp-final,win7-final}`，详细范围见 [旧系统验证记录](LEGACY_MUSIC_WINDOW_VALIDATION_20261028.md#xp-ie8-补测与按钮修补p11)。

已部署 `ttpbrowser.dll 2026.10.28p11`，758,784 字节；独立包 `ttpbrowser-2026.10.28p11.zip` 为 347,768 字节。包检查、XP/Win7 静态导入检查通过。主程序保持 p6。

## XP / Win7 音乐窗回归与 GPU 选项 / 2026-10-28 p10

新增「选项 → 网络连接 → 音乐窗 → 关闭 GPU 加速」，保存到 `BrowserDisableGpu`。
Win7 缺省使用软件绘制，其余系统保留原默认值。两种网页视图共用配置；旧宿主 ABI
前缀与不完整尾部字段通过受保护内存页测试。Sayobot 只使用 WebView2，XP 和缺少
Runtime 的 Win7 不显示入口，个人媒体库继续使用 WebView2 → IE → 原生回退。

XP、Win7 各 7 个基础套件、300 个 PASS，全套退出码为 0。两台虚拟机的 17 个
传输场景通过，包括真实 `ttp_https` 的 TLS 1.2/1.3。Win7 WebView2 109 软件绘制
通过真实个人媒体库及 Sayobot 的试听、详情播放、文件名和三种 OSZ 下载测试。
主程序菜单、来源管理、九宫格缩放、吸附、淡入淡出、打开状态恢复及重复播放一并验证。
环境、范围、失败诊断与最终产物见 [详细记录](LEGACY_MUSIC_WINDOW_VALIDATION_20261028.md)。

## 恢复 Sayobot 页面内试听 / ttp_osu 2026-10-28 p2

原因：p1 为避免详情播放在网页与主程序同时出声，全局阻止了 `HTMLMediaElement.prototype.play`，也阻止了卡片试听和网页播放器的继续播放。

p2 恢复浏览器原生播放调用及其 Promise/错误行为，只观察实际试听媒体对象；详情「播放」仍在捕获阶段接管。详情歌曲信息成功取得、准备提交播放器时，暂停已有网页试听；优先调用站点可见的暂停按钮同步图标。没有修改 WebView2 静音、元素静音或音量。卡片试听不提交到主程序播放列表。

真实 WebView2 隔离宿主点击 SID 2566051 的「试听」，请求 `https://cdnx.sayobot.cn:25225/preview/2566051.mp3`，媒体实际为 `paused=false`、`currentTime=0.312733`、`muted=false`、`volume=0.5`。验证时间继续推进、网页按钮暂停/继续、详情转入原生时试听停止且没有新增网页 play 调用、仅一次原生歌曲提交及正确歌名/歌手，再次卡片试听仍在网页播放且不新增提交。插件 ABI 探针 46/46，通过编译与 XP/Win7 静态导入检查；没有重复旧系统虚拟机和曲包下载测试。

仅更新 `rebuild/build/Release/AddIn/ttp_osu.dll` 为 `2026.10.28p2`，主程序保持 p5、音乐窗保持 p9。包为相邻插件的 `build/Release/ttp_osu-2026.10.28p2.zip`。证据：`out/sayobot-preview-20261028p2` 内的 `live/preview/driver.log`、`host.log`、`web-preview-playing.png`、`probe.log`、构建日志、部署前 DLL 备份和 `deployment.json`。

## Sayobot 详情播放的列表标题 / 主程序 2026-10-28 p5

修复主程序播放列表的显示判定：此前仅个人媒体库内部地址和 osu! 本地媒体库在文件名模式下使用服务歌曲信息，Sayobot 的真实 HTTPS 音频地址虽已收到标题/歌手，却回退为完整 URL。现在对带 `TTPlayer.Tid` 的 HTTP/HTTPS 音乐窗条目同样采用歌手/标题；有效的自定义标签格式继续优先。普通粘贴 URL、没有来源标识的网络地址和本地文件维持原显示规则。

本次改动为 `track_metadata.h` 的音乐窗条目识别及 `player_window_playlist.cpp` 的标签字段回退和标题格式化，不修改 URL、下载逻辑或网页脚本。既有条目只要仍保存歌曲信息及来源标识，重新显示即可生效。

- 真实 PlayerWindow 存在/缺失/不兼容三组宿主测试通过；新增 Sayobot 实际入列、文件名模式、自定义/空/无效标签格式、标题单独存在、解码标签快照更新、TTBL 保存重载及普通 URL/本地文件对照。
- 隔离真实播放器经 `SubmitMusicBrowserTracks(..., 0, 1, 1)` 接收与详情页相同的 SID 2566051、BID 5707607 完整音频地址。通过实际 `ttp_https` 网络流及解码器播放超过 1 秒，在打开完成与信息刷新后检查实际列表文本为 `Aurora Sky - Butterfly In Ashes`，再次保存/重载 TTBL 仍一致。此测试直接覆盖主程序收到详情播放请求后的流程；未重新执行网页点击或曲包下载测试。
- 主程序及别名 EXE 更新为 `2026.10.28p5`，XP/Win7 静态导入检查通过。音乐窗保持 p9、osu 插件保持 p1，未重复旧系统虚拟机测试。

证据：`out/sayobot-title-20261028p5/host.log`、`live/results.log`、`live/sayobot-playlist.bmp`、构建日志及 `deployment.json`。旧主程序保存在 `deployment-before/TTPlayerRebuild.exe`，用户配置与媒体来源文件未改写。

## Sayobot 下载、详情播放与服务器文件名 / 2026-10-28 p9

配套 `TTPlayerRebuild.exe 2026.10.28p4`、`ttpbrowser.dll 2026.10.28p9`、`AddIn/ttp_osu.dll 2026.10.28p1`。插件下载页 ABI 2 追加站点脚本和音频识别，保留 ABI 1 前缀；主程序及音乐窗按字段大小检测可选回调。

- 修复卡片云下载和详情「下载／无视频／mini」的接管。此前空的下载目录导致入队失败但网页不显示错误；现在恢复主程序原生默认目录，操作结果短暂提示。详情按钮在捕获阶段接管，不依赖网页的下载冷却计时。
- 详情「播放」读取当前难度对应的完整音频、标题和歌手，追加当前播放列表并立即播放。阻止网页原处理函数及 HTML 媒体播放调用；测试中的独立 `new Audio()` 保持 `muted=false`、音量 `0.41`、`paused=true`。没有调用 WebView2 静音，也不修改媒体或系统音量。
- 宿主从 `ttp_https` 最终响应的 `Content-Disposition` 取文件名，经 `IStream::Stat` 返回下载队列，不额外请求。采用 `filename*` 的 UTF-8/Latin-1 解码与 `filename` 回退；校验 Windows 文件名与 `.osz` 扩展。仅曲包启用服务器命名。未启用覆盖时重名自动编号，最终名字同步下载列表和持久记录，同 URL 重复点击仍复用任务。
- 按用户提示扩大隔离测试音乐窗至 1300×1040，实际点击卡片与详情按钮。未修改站点右下角悬浮按钮。首次追加截图曲包的测试遇到站点重新弹出的说明对话框，修正测试等待/点击「确定」后最终通过。

最终实际下载结果（四个 ZIP 全部通过成员 CRC 校验，包含 `.osu` 谱面）：

| 文件 | 字节 | 来源 |
| --- | ---: | --- |
| `2562231 glass beach - soft!!!!!!.osz` | 13,756,035 | 用户截图卡片，同浏览器服务器名称 |
| `2566051 Aurora Sky - Butterfly In Ashes.osz` | 43,119,469 | 卡片及详情完整包 |
| `2566051 Aurora Sky - Butterfly In Ashes (1).osz` | 8,158,372 | 无视频包，同名自动编号 |
| `2566051 Aurora Sky - Butterfly In Ashes (2).osz` | 5,663,922 | mini 包，同名自动编号 |

验证：插件 ABI 探针 46/46；音乐窗 CTest 7/7（新增服务器命名、中文、畸形编码、危险路径、重名、队列持久化与普通音乐命名不受影响的用例）；独立发行 DLL 合同测试通过。此前本轮 PlayerWindow 组件存在/缺失/不兼容三组通过，包含空下载目录恢复。最终真实 WebView2 宿主验证正确歌名/歌手、追加并立即播放模式、一次显式播放提交、外域音频拒绝、原生音频流读取和 Seek、卡片及三个详情下载按钮。未重复 XP/Win7 虚拟机实测；集成与独立 DLL 的 XP/Win7 静态导入审计通过，第三方现代网页的旧 IE 运行情况未据此宣称通过。

证据：`out/sayobot-fix-20261028p9` 内的 `probe-final.log`、`ctest-filename.log`、`host2`、`detail-filename-final/detail/driver.log`、`host.log`、`detail-play-download.png`、`download-files.json`、`standalone-contract.log`、构建日志及 `deployment.json`。运行包为 `build/Release/ttpbrowser-2026.10.28p9.zip` 和相邻插件的 `build/Release/ttp_osu-2026.10.28p1.zip`；未发布远端发行。

已更新正式 `rebuild/build/Release` 三个组件。最终复制前后的六个现有 XML/媒体来源/页面选择/下载记录文件哈希一致。最终 DLL 复制前备份在 `deployment-before-final`；主程序此前已由构建更新，因此该目录不是主程序 p3 的历史备份。部署 DLL 大小 749,568 字节，SHA-256 `0b86e980ff0de7c974db0f986a00eb522c81c5523a797aa10dbecdf6a1ea54b0`。

## Sayobot 下载页与游戏程序识别 / 2026-10-28 p8

配套 `TTPlayerRebuild.exe 2026.10.28p3`、`ttpbrowser.dll 2026.10.28p8`、`AddIn/ttp_osu.dll 2026.10.28`。插件提供站点名称、首页和 OSZ 下载 URL 识别；音乐窗负责网页与下载队列。主程序复用插件安装接口的身份判断，osu!／lazer 配置至少一个识别成功才显示边框及齿轮入口。缺失、旧组件、两路径无效均隐藏；修改路径后动态同步，已打开且失效的镜像站返回个人媒体库，保存的镜像站选择同样受启动检查约束。

- 插件实际 DLL 的下载页 ABI 探针 34 项通过：完整/无视频/精简曲包、试听排除、域名/路径检查、参数与缓冲边界。
- CTest 7/7，通过新增可选回调的截断结构与保护页边界、OSZ 文件保存和错误文件名拒绝；PlayerWindow 的音乐窗组件存在、缺失、不兼容三组集成测试通过。
- 六组隔离宿主使用实际插件和本机实际游戏程序：组件缺失、旧版、两路径无效、仅 stable 有效、仅 lazer 有效、动态失效与恢复。验证两个入口、已打开页面回退和恢复。
- 真实 WebView2 页面点击试听，观察到浏览器请求 `cdnx.sayobot.cn:25225/preview/2562231.mp3`；点击下载取得 `Sayobot 2562231.osz`（13,756,035 字节），状态 complete，ZIP 文件头正确，页面留在原站点，播放列表提交次数为 0。第三方页面无 `window.external.ttplayer`。测试脚本曾因站点延迟弹窗及脱离 DOM 的 Audio 对象假设失败，修正测试等待与请求观测后通过；未据此修改第三方页面。
- 主程序、集成/独立音乐窗与插件编译通过；XP/Win7 静态导入审计、独立音乐窗单 DLL ZIP 校验通过。没有执行本次 XP/Win7 虚拟机实测，现代第三方站点的 IE 兼容性不作保证。

证据位于忽略目录 `out/osu-download-20261028`：`probe.log`、`ctest.log`、`host1`、`matrix-final`、`live4`、构建日志和部署清单。运行包分别为 `build/Release/ttpbrowser-2026.10.28p8.zip` 与相邻插件的 `build/Release/ttp_osu-2026.10.28.zip`。未发布远端发行。

已更新 `rebuild/build/Release` 的主程序、音乐窗和 `AddIn/ttp_osu.dll`，旧组件保存在上述证据目录的 `deployment-before`。部署前后播放器 XML、媒体来源数据库和页面选择文件哈希一致；最终版本与 SHA-256 见 `deployment.json`。

## 边框菜单统一与打开状态恢复 / 2026-10-28 p7

更新 `TTPlayerRebuild.exe 2026.10.28p2` 与 `ttpbrowser.dll 2026.10.28p7`。音乐窗边框菜单使用主程序现有的菜单绘制、图标栏、分隔线和快捷键处理；新增「选项(P)…」，使用歌词秀等窗口菜单的设置图标并直接打开「网络连接 → 音乐窗」，移除「音乐网页」。右下角齿轮的设置入口继续直达媒体来源。菜单回调按宿主结构大小检测，旧宿主继续使用原生弹出菜单；没有设置回调时禁用选项项。

启动时恢复已保存的 `BrowserVisible`：上次打开则打开，上次关闭则关闭；新配置仍默认关闭。显式启用 `BrowserAutoStartup` 时强制显示，迷你模式的临时隐藏不覆盖正常模式记录。

- CTest 7/7 通过，包含新增回调的截断结构/保护页边界、选项分派、菜单取消及旧宿主回退。
- 实际 PlayerWindow 的 DLL 存在、缺失、不兼容三种隔离测试通过。真实菜单跟踪验证所有条目进入共用绘制、绘制/测量消息转发、设置图标与歌词菜单相同、按 P 打开「网络连接 → 音乐窗」与重复打开后的资源清理；保存打开/关闭并重建主程序验证状态恢复，关闭音乐窗后保存值为关闭。
- 正式 Release 程序中右键打开音乐窗菜单并点击「选项(P)…」，确认菜单样式与选项窗口正确。截图为 `out/menu-state-20261028p7/border-menu.png` 和 `options.png`。
- 集成 EXE、集成/独立 DLL 构建和单 DLL ZIP 校验通过；XP/Win7 静态导入审计通过。本次没有重复旧系统虚拟机实测。

已部署到 `rebuild/build/Release` 并启动预览。日志、部署前备份和最终哈希位于 `out/menu-state-20261028p7`，发行包为 `build/Release/ttpbrowser-2026.10.28p7.zip`，未发布远端发行。

| 文件 | SHA-256 |
| --- | --- |
| Release `TTPlayerRebuild.exe` | `71ab024b924a0df5911f2ce089913a5ef82fce70c41536a4caed90460cdedfad` |
| Release `ttpbrowser.dll` | `19948273af0d222164c9b781bbfddcbe31f2ada95042d4de9a6638772553cd72` |
| `ttpbrowser-2026.10.28p7.zip` | `cc073c991e049497f77081eb88f5314909ddb0240f5780485a72a1fe3eccd061` |

## 移除网页底部状态字段 / 2026-10-28 p6

`ttpbrowser.dll 2026.10.28p6` 移除 WebView2 与 IE 页面的底部状态文字节点和预留空间。播放、添加、下载成功后不再增加一行；翻页、操作按钮和齿轮保持同一行。读取进度继续使用列表内的加载文字，操作错误在列表内显示，不占用底部工具栏。

- 真实 WebView2 宿主通过播放、添加、下载、来源菜单、搜索及 320～680 像素布局检查；操作前后底部高度一致，状态字段不存在，下载以宿主响应和实际文件验证。截图：`out/media-status-20261028p6/live/compact-songs.png`。
- 本机强制 IE 页面通过列表、添加、下载、来源切换、设置入口及导航限制检查；截图：`ie/ie-navidrome.png`、`ie/ie-openlist.png`。首次使用未定义的 `--ie` 参数实际选择了自动引擎，已按现有测试入口不传第三参数重跑，最终日志为 `ie-final.log`。
- 集成及独立 DLL 构建、静态 XP/Win7 导入审计、单 DLL ZIP 校验通过；本次没有重复旧系统虚拟机测试。

已更新 `rebuild/build/Release/ttpbrowser.dll`；主程序保持 `2026.10.28p1`，媒体来源配置未改写。日志、部署前备份及最终哈希位于 `out/media-status-20261028p6`，发行包为 `build/Release/ttpbrowser-2026.10.28p6.zip`。

## 音乐窗淡入淡出、封面悬停与启动默认值 / 2026-10-28 p5

更新 `TTPlayerRebuild.exe 2026.10.28p1` 与 `ttpbrowser.dll 2026.10.28p5`。DLL 通过按 `size` 检测的可选宿主回调请求显示/隐藏，主程序复用现有皮肤窗口动画，遵循「淡入淡出」、透明度和透明色键设置。关闭开始即发布隐藏状态；淡出结束后隐藏 HWND 并恢复配置透明度。菜单、主窗口按钮、F11、关闭按钮和 Esc 共用此路径，快速切换不会留下错误的可见状态。旧宿主缺少回调时保持即时显示/隐藏兼容行为。

专辑、歌手、歌单网格的封面播放按钮默认隐藏，悬停封面时淡入，移出后隐藏；键盘焦点可显示按钮，鼠标点击造成的普通焦点不会使按钮一直显示。按钮隐藏时不截获封面点击。播放后仍保留当前网格。

新配置及缺少 `BrowserAutoStartup` 的旧 XML 默认不在启动时显示音乐窗。显式保存的开关值继续生效，底部复选框仍可修改。实际预览播放器启动后确认音乐窗默认关闭。

- CTest 7/7 通过，包含旧宿主及截断可选回调边界检查。
- 实际 PlayerWindow 宿主存在/缺失/不兼容三种 DLL 状态通过；新增动画检查覆盖初始/中间/终点透明度、配置透明度恢复、色键保持、关闭/Esc、快速显示/隐藏/显示和关闭动画选项。默认值及缺失 XML 属性为关闭，显式启用保存重载保持启用。
- 真实 WebView2 页面验证默认隐藏、封面悬停显示、移出隐藏、键盘可达与聚焦显示、点击播放后移出隐藏；原有播放/添加/下载、搜索、来源菜单和窄窗布局检查继续通过。
- 集成 EXE、集成/独立 DLL 构建和单 DLL ZIP 校验通过；XP/Win7 静态导入审计通过。本次未重复旧系统虚拟机实测。

最终宿主、单元和页面日志：`out/media-fade-20261028p5/host.log`、`ctest.log`、`web-final.log`、`live-final/host-results.log`。悬停前后截图为 `live-final/albums.png`、`live-final/cover-hover.png`。已替换正式预览 EXE/DLL 并启动，来源配置未由本次部署改写；部署前文件在 `deployment-before`，最终版本和哈希见 `artifacts.json`。发行包为 `build/Release/ttpbrowser-2026.10.28p5.zip`，未发布远端发行。

## 播放列表歌曲信息与来源恢复 / 2026-10-28 p4

更新 `TTPlayerRebuild.exe 2026.10.28` 与 `ttpbrowser.dll 2026.10.28p4`。音乐窗提交曲目时，歌曲名、歌手同时写入曲目字段及标签表；播放列表的有效标签格式可直接使用它们。文件名模式或空/无效标签格式下，个人媒体库条目回退为「歌手 - 歌曲名」，缺少歌手时显示歌曲名，OpenList 显示文件名。此前仅保存曲目字段的条目也支持标签格式。普通本地文件保留原有显示规则。

设置界面、默认来源名和菜单统一显示 `OpenSubsonic`。加载旧配置时将两种原组合名称规范为该名称，保留来源 ID、凭据和自定义名称。按用户要求，正式播放器恢复两个原预览来源：OpenSubsonic（本机 Navidrome，`http://127.0.0.1:45331`）及 OpenList（`http://127.0.0.1:52441`）；复用现有隔离服务和原来源标识，继续扫描用户指定的测试音乐目录。

- CTest 7/7 通过，包括旧名称迁移、来源身份与凭据保持。
- 实际 PlayerWindow 宿主检查 DLL 存在/缺失/不兼容三种状态；标题回归覆盖文件名模式、空标签格式、显式标签格式、旧条目、无歌手、OpenList 文件名、空元数据刷新、TTBL 保存重载和普通本地文件。
- 真实 Navidrome/OpenList 接口八项通过；WebView2 验证封面播放、单曲播放/添加/下载、来源菜单名称、搜索和布局。原生流读取/Seek、FLAC/M4A 解码及卸载通过。
- 正式播放器重新启动后，个人媒体库专辑加载正常；此前已加入的《夜航星》显示歌曲信息并实际播放成功。页面播放按钮由隔离的真实 WebView2 宿主验证，正式浏览器子窗口点击受到 computer-use 的跨进程目标限制。截图位于 `out/media-title-20261028p4/deployed`。
- 集成 EXE、集成/独立 DLL 构建及单 DLL ZIP 校验通过；静态导入审计通过。本次未重复旧系统虚拟机测试。

验证材料位于 `out/media-title-20261028p4`：`host-final.log`、`ctest.log`、`live.log`、`web.log` 和 `live/host-results.log`；部署前 EXE、DLL、媒体配置和 XML 备份位于 `deployment-before`。最终文件哈希为 `artifacts.json`，来源名称/地址的脱敏记录为 `restored-sources.json`。发行包为 `build/Release/ttpbrowser-2026.10.28p4.zip`，未发布远端发行。

## ttp_https 接入、来源列表绘制与旧系统兼容 / 2026-10-28 p3

`ttpbrowser 2026.10.28p3` 的个人媒体库登录、列表、搜索、封面、歌词和地址解析优先调用 `AddIn/ttp_https.dll` 新增的 ABI 8，支持 HTTP/HTTPS GET、JSON POST 和 Authorization。组件缺失、损坏、接口不足，或组件在发送源站请求前明确要求系统代理处理时，回退 WinINet。TLS/证书失败、超时、取消、HTTP 状态和读取中断均不会触发重发。接口、请求限制和代理行为见相邻仓库的 [HTTP_ABI8.md](../../mbedtlsmin/docs/HTTP_ABI8.md)。音频播放、下载继续使用主程序 ABI 7；历史音乐网页的网络请求仍由浏览器引擎处理。

媒体来源页改为原生子对话框，列表批量更新并启用双缓冲；图标按钮的普通绘制、按下、禁用和打印路径共用缓冲绘制。布局只移动尺寸确实发生变化的控件，消除重复布局引起的重绘。正式选项页复查还发现子对话框会落在标签控件后面，已修正窗口层级，并增加宿主内的遮挡检查。实际显示截图为 `out/media-source-paint-20261028p3/deployed/source-list.png`。

XP 实测发现两项兼容问题并修复：URL 拆分使用调用方分配的缓冲区；两个动态 DLL 使用 YY-Thunks 的 DLL 入口包装，正确初始化静态 TLS，避免关闭时 CRT 析构崩溃。XP 跳过不支持的平台 WebView2 初始化，继续 IE/原生回退。

- CTest 7/7 通过；最终绘制版本的设置专项 29 项通过。检查按钮实际画面与打印路径一致，重复等尺寸布局不触发列表重绘，40 次拉伸/选择/隐藏/显示后行、选择和 GDI 对象计数保持正确。
- 本机传输矩阵 19/19 通过，涵盖缺失/旧组件、显式原生回退、GET/POST、认证头、空 POST、错误与取消不重试，以及重定向、401、8 MiB 上限、截断和头注入。未信任 TLS 的连接计数为 1。
- XP SP3 x86、Win7 SP1 x64 实测同一 DLL：HTTP、TLS 1.2、TLS 1.3 各 30 项 ABI 检查；音乐窗 GET/POST 两项、响应边界七项和原生设置 29 项通过。TLS 使用测试私有 CA，没有修改系统信任库。
- 两台虚拟机的实际宿主创建/设置/皮肤/吸附/关闭以及正式 EXE 隔离启动通过。最终新增的来源页层级检查在本机、XP、Win7 均通过。
- 本机真实 Navidrome/OpenList 接口八项通过；真实 WebView2 验证专辑、搜索、封面、来源菜单、单曲/批量操作、320～680 像素布局和下载。宿主音频 Read/Seek、FLAC/M4A 解码、卸载回归通过。旧系统验证范围为上述原生界面、传输和宿主检查，未穷举所有服务或企业代理环境。
- 集成及独立 Release 构建、两个 ZIP 包装校验通过。没有发布远端发行。

主要日志：`out/ctest-https-paint-final.log`、`out/settings-delivery.log`、`out/host-https-paint-final.log`、`out/live-https-api8.log`、`out/live-web-https-api8-final.log`。传输矩阵及真实页面证据位于 `out/https-20261028p3`；XP、Win7、最终宿主和部署证据位于 `out/media-source-paint-20261028p3`。

已替换正式 `rebuild/build/Release/ttpbrowser.dll` 和 `AddIn/ttp_https.dll`，并重新启动播放器。EXE SHA-256 保持 `567f8461109b5ace9a550dc062e3c7204d288a2cd6f4b22e362b05b77f522751`；媒体配置 SHA-256 保持 `7225746405473455c97717ef59011e0abdf00e6b4accc36b7d23c5386186967c`。部署前配置已为空来源列表，本次保留其状态。旧 DLL、加密配置和部署前哈希备份在 `out/media-source-paint-20261028p3/deployment-before`。

| 产物 | 字节数 | SHA-256 |
|---|---:|---|
| Release 集成 `ttpbrowser.dll` | 731136 | `a07b4182e4f8bbd6024db5837f90d388fc5f4e9b06ac031e50300beabac3c93a` |
| Release `AddIn/ttp_https.dll` | 474624 | `e570d39d39456c4a09b78a6d4443533fc1d33cb482ec5037c2b2d3ded67262ca` |
| 独立 `ttpbrowser.dll` | 731136 | `6bb7e3a488153432b6a40e344cae576e9e1973e8b9908a81718822845e0a8f06` |
| `ttpbrowser-2026.10.28p3.zip` | 332942 | `52ab2185426322000bb94a2cc7d0f6a7753395165e8b341d104fa36cc8269dc5` |
| `ttp_https-2026.10.28.zip` | 285653 | `6912d8d0eab7fa7ad817ae1f6f42b28d18e348b9f7797da6233885462f765f7c` |

## 可编辑媒体来源与选项图标对齐 / 2026-10-28 p2

`2026.10.28p2` 将媒体来源设置改为「名称 / 类型」列表，支持添加、删除、编辑、排序和同类型多个服务。双击行、Enter/F2 或右下角编辑图标打开独立编辑窗口；确认后持久保存，取消不修改配置。音乐窗右下角齿轮展开菜单，首行为「设置」，随后按配置顺序显示来源名称和当前来源标记。WebView2、IE 和原生回退使用相同交互。

工具栏复用现有选项窗口的添加、删除、上移、下移和编辑位图；优先从同目录 `ttpres.dll` 加载，DLL 内含相同位图作为回退。按钮为 15×14 对话框单位，列表边距、按钮排列、字体及悬停、按下、禁用状态绘制与现有选项对齐。正式播放器内已检查列表和编辑窗口，截图位于 `out/media-sources-20261028p2/deployed/source-list.png`、`source-editor.png`。

- CTest 7/7 通过。媒体配置测试覆盖稳定 ID、重命名和排序后保持当前来源、重复 ID 拒绝、删除后的旧链接拒绝、空列表重启保持及重新添加。最终图标和字体调整后，原生设置专项测试再次通过。
- 原生设置专项检查名称与类型列、双击编辑、取消、名称校验、同类型新增、连接测试、排序、切换类型分配新 ID、保存失败时保持原配置、删除当前来源及清空后重新添加。
- 隔离的真实 WebView2、IE、原生宿主使用重排后的三个来源验证切换，覆盖 OpenList 位于首项及两个 OpenSubsonic 来源。WebView2 验证齿轮菜单顺序、当前项、键盘操作、播放、添加、下载、搜索、封面和详情；IE 与原生验证切换、设置跳转、提交、下载、HTTP 读取/Seek 及解码。
- 现代页面与 ES5 页面另验证空来源、64 个长名称、320 像素宽菜单滚动及名称按文本显示。真实宿主的 DLL 存在、缺失、不兼容回归通过。
- 集成及独立 Release 构建、单 DLL ZIP 校验通过。最终静态导入审计为 16 DLL / 366 imports；未执行旧系统虚拟机实测。

日志：`out/ctest-media-sources.log`、`out/media-sources-settings-final.log`、`out/media-sources-web.log`、`out/host-media-sources.log`、`out/package-media-sources-final.log`。三种渲染路径、菜单边界及截图材料位于 `out/media-sources-20261028p2`。

已替换 `rebuild/build/Release/ttpbrowser.dll` 并重新启动正式播放器，DLL 为 723456 字节；原 DLL 备份在 `out/media-sources-20261028p2/deployment-before`。EXE 哈希仍为 `567f8461109b5ace9a550dc062e3c7204d288a2cd6f4b22e362b05b77f522751`。正式媒体配置在查看列表、打开并取消编辑后哈希不变。来源菜单交互由隔离的真实渲染宿主验证，正式播放器的浏览器子窗口因 computer-use 的跨进程目标限制未直接操作。未发布远端发行。

| 产物 | 字节数 | SHA-256 |
|---|---:|---|
| Release 集成 DLL | 723456 | `3cd6d32e64d03c537f67b6d822b3f5a24c2b360d68e23b21a5fd5f69d0c79d81` |
| 独立 DLL | 723456 | `504bf08a27f3f1dffdd5aace8bd0f3d34c4ac585b116eca856d624972418fca2` |
| `ttpbrowser-2026.10.28p2.zip` | 337757 | `36c6ea00415df5b6b00cf0cf99212c6c3ccbf82bacc35afb4e06060a273ede92` |

## 紧凑音乐窗与封面播放 / 2026-10-28 p1

`2026.10.28p1` 删除页面品牌标题、来源工具栏和固定提示，缩减搜索、分类、歌曲行的留白。单曲右侧为播放、添加、下载三个图标；网格封面右下角增加直接播放。底部左侧为页码，右侧为紧凑批量按钮，最右为纯齿轮设置入口。IE 与原生回退同步压缩工具栏，来源切换集中到主程序选项并持久保存。

- CTest 6/6 通过；新增来源选择持久化、容器跨页收集、歌手去重、取消和 1000 首上限检查。最终收集时限调整后媒体单元测试再次通过。
- 真实 WebView2 验证封面播放保留网格、单行三按钮仅操作对应歌曲且不改变勾选状态、多选操作、搜索、详情、下载、设置跳转及来源切换。
- 320/440/536/680 像素宽度的页码、批量按钮、齿轮不重叠，无横向溢出；另以七页模拟数据验证 WebView2 / ES5 的前后翻页、数字跳页和 320 像素宽五个页码布局。
- 真实 Navidrome / OpenList 的 IE 与原生回退通过来源切换、提交曲目、下载、HTTP 读取/Seek、FLAC/M4A 解码和卸载检查。IE 测试路径包含空格及中文；回退模拟只影响测试进程。
- 集成及独立构建、单 DLL ZIP 校验通过，静态导入仍为 15 DLL / 340 imports。未运行旧系统虚拟机测试。

验证记录：`out/media-compact-20261028p1`、`out/ctest-compact-20261028p1.log`、`out/media-compact-final-unit.log`。发行包 `build/Release/ttpbrowser-2026.10.28p1.zip`，329472 字节；DLL 为 701440 字节。

本次仅替换 `rebuild/build/Release/ttpbrowser.dll`，原 DLL 备份在 `out/media-compact-20261028p1/deployment-before`；EXE 哈希保持 `567f8461109b5ace9a550dc062e3c7204d288a2cd6f4b22e362b05b77f522751`。已重新启动正式播放器，实际网格截图为 `out/media-compact-20261028p1/deployed/music-window.png`。正式宿主内的浏览器子窗口被 computer-use 判为不同进程，因此未强行操作该子窗口；歌曲页交互及布局由隔离的真实 WebView2 宿主验证。没有发布远端发行。

| 产物 | SHA-256 |
|---|---|
| Release 集成 DLL | `a694fd6330f0a3cb23ec03ca14f62e97a7b4a7e75345260ea8e670732b009182` |
| 独立 DLL | `27aaa109a1cc215c29192038f064adf41a1fb5b531aa65f2e9e1d1ef4dfe14ce` |
| ZIP | `16e30c61ff57998c5e3655361d368f36d542b634ce93e8a4d66321cdb50bc14d` |

## 回退链、合并设置与旧标签乱码 / 2026-10-28

`2026.10.28` 将个人媒体库的回退顺序改为 WebView2 → IE/MSHTML → 原生列表。IE 使用独立 ES5 资源页及受限消息接口；处理嵌入 HTML 的 ActiveDocument 加载事件，避免被当成普通文件下载取消。

主程序「选项 → 网络连接」分为「连接设置」（代理服务器、MusicBrainz）和「音乐窗」（在线试听、歌曲下载、媒体来源）。原网页、原生媒体列表中的设置按钮都跳转到宿主选项；新表单由 DLL 创建为选项页子窗口。来源凭据继续由 DPAPI 保存。此次同时更新 Release EXE 和 DLL。

发现 Navidrome 的两首 MP3 原有 GBK 标签被解释为 Latin-1。展示层恢复「一帘幽梦 / 许茹芸」「上海滩 / 叶丽仪」及相应专辑，采用严格编码往返检查；正确中文、日文、西文重音字符不变。没有修改音频文件、服务端数据、来源 ID 或 OpenList 文件路径。

验证：

- CTest 6/6；含媒体协议、凭据、乱码恢复、合法多语言文本保持、桥接 ABI、下载及鼠标行为。
- 原历史网页 WebView2 / IE / 自动回退各 11/11 通过；仍无需外置 WebView2Loader.dll。
- 宿主存在、缺失、不兼容三种 DLL 状态通过，检查选项入口、嵌套页面互斥、设置保存、下载快捷入口、吸附与九宫格。
- 真实 Navidrome / OpenList：WebView2、IE、原生三条路径均通过浏览、原生列表提交、下载、HTTP Read/Seek、FLAC/M4A 解码。WebView2 测试另覆盖搜索、封面、页面宽度、外部请求/导航阻断、陈旧列表拒绝、设置跳转和乱码恢复。
- 用阻塞的 WebView2 用户数据目录验证向 IE 回退；用仅作用于测试进程的注册自由 COM 激活上下文模拟 IE 不可用，验证原生回退。IE 成功路径的测试目录包含空格和中文。
- 实际 Release 选项页截图与「测试连接」成功截图：`out/media-options-20261028/deployed`；乱码修复截图：`out/media-options-20261028/web-evidence/corrected-titles.png`。
- 独立 DLL 包装校验通过。DLL 静态导入审计 15 DLL / 340 imports，主程序 23 DLL / 751 imports；未执行 XP / Win7 VM 实测。

验证材料：`out/media-options-20261028/host1`、`legacy-runtime`、`web-evidence`、`IE 测试 final`、`native-final/host-activation-results.log`，以及 `out/ctest-20261028.log`。没有远端发布。

| 产物 | 字节数 | SHA-256 |
|---|---:|---|
| `rebuild/build/Release/TTPlayerRebuild.exe` | 4471296 | `567f8461109b5ace9a550dc062e3c7204d288a2cd6f4b22e362b05b77f522751` |
| `rebuild/build/Release/ttpbrowser.dll` | 686592 | `3c46e7c18afb7ed5b6e0d1d29f896f377b2afdbdd9cc635243933f89d2584a98` |
| `ttpbrowser/build/Release/ttpbrowser.dll` | 686592 | `c4370eecf23548564676e487144a84961c242e9d34ae01dd9236a07e219b634c` |
| `ttpbrowser/build/Release/ttpbrowser-2026.10.28.zip` | 323721 | `c80ababc7869c013e7a454986b686391d8127a9dfc74a5fd081afd29cc27082c` |


## 个人媒体库网页化 / 2026-10-27

`2026.10.27p3` 将个人媒体库默认界面改为 WebView2 网页。HTML/CSS/JavaScript 作为 DLL 资源加载，专辑使用卡片，歌曲和 OpenList 文件采用自适应列表。认证、查询、播放及下载继续走原生实现。缺少 Runtime、初始化失败或强制 IE 模式时回退原生列表；旧音乐网页、下载管理和皮肤外框继续使用现有实现。

- 独立及集成 Release 构建、单 DLL ZIP 校验、XP/Win7 静态导入审计通过；未执行旧系统 VM 实测。
- CTest 6/6 通过。旧网页 WebView2 / IE / 自动回退各 11/11 通过；宿主存在、缺失、不兼容、吸附与九宫格回归通过。
- 隔离的真实 WebView2 页面通过专辑/歌曲浏览、返回、搜索、封面、设置测试/保存、多来源切换、原生播放/加入列表/下载检查。
- 320、440、680 像素宽度没有页面横向溢出，设置表单可纵向滚动；截图位于 `out/media-web-20261027/verified`。
- 凭据不会通过读取配置接口返回网页，过期列表操作被拒绝，页面请求外部网络及导航被限制。修正了拒绝外部导航时误将现有页面判为加载失败的问题。
- WebView2 缺失与初始化失败的原生回退通过；另增加缓存目录预检查并验证，以免已知目录异常触发 Runtime 独立错误弹窗。
- 实际 Release 播放器已重新启动并显示网页音乐窗；部署截图位于 `out/media-web-20261027/deployed`。页面操作提交的内部来源可在宿主读取、Seek，并通过 FLAC/M4A 原生解码。

部署仅替换 Release 的 DLL，主程序 EXE 哈希保持不变，继续使用 p2 的解析接口。原文件备份在 `out/media-web-20261027/deployment-before`。没有修改用户的媒体服务配置，没有发布远端发行。

DLL 为 675,840 字节（660 KiB），ZIP 为 317,818 字节。未增加外部 HTML、WebView2Loader DLL 或前端依赖包。

| 文件 | SHA-256 |
| --- | --- |
| 集成 `ttpbrowser.dll` | `8da2cc10d306729bc0829ad37616e5532a88e3c00d6e952131a761cf0e9c486d` |
| 独立 `ttpbrowser.dll` | `eca4d6eb70a13424de98d43befef1858fd3b628e2179eba8fb525537cb7178c1` |
| `ttpbrowser-2026.10.27p3.zip` | `e72d59c21899702f1f0b9214e08fa296983e57b054d00b8a572488bc0c7e4e55` |

## OpenSubsonic / OpenList 接入 / 2026-10-27

`2026.10.27p2` 增加 DLL 内的原生个人媒体库、OpenSubsonic 与 OpenList 协议适配，并通过可选宿主解析器把歌曲交给现有播放和下载链路。使用及实现边界见 [PERSONAL_MEDIA.zh-CN.md](PERSONAL_MEDIA.zh-CN.md)。本次需要同时更新主程序与 DLL。

- 独立及集成 Release 构建成功，CTest 6/6 通过。
- 使用 Scoop Navidrome 0.64.2 和隔离 OpenList 4.2.9，实际扫描用户指定音乐目录；两个来源的查询、原生界面、加入列表和下载均通过。
- 两个来源的播放及下载 URL 均由主程序 HTTP producer 解析，流读取与 Seek 通过；FLAC / M4A 原生解码及定位通过。两份下载文件 SHA-256 与原音乐文件一致。
- WebView2、IE、自动回退各 11/11 通过；宿主的 DLL 存在、缺失、不兼容及吸附、九宫格、鼠标行为回归通过。
- 配套 Release 产物在隔离目录实际启动、显示音乐窗和正常关闭，退出码 0、捕获异常 0。
- 单 DLL 包白名单、x86、校验和、XP/Win7 静态导入审计通过。未执行旧系统 VM 验证。

已更新 `rebuild/build/Release/TTPlayerRebuild.exe` 与 `ttpbrowser.dll`，并预填本机测试来源的 DPAPI 加密配置。原有主程序 XML 内容保持不变。测试服务只监听本机，没有安装系统服务或开机启动。备份、截图、日志、下载哈希和最终校验记录位于 `out/media-integration/`；未发布远端发行。

集成与独立 DLL 均为 601,600 字节，独立 ZIP 为 287,242 字节。两种构建的二进制哈希分别记录如下：

| 文件 | SHA-256 |
| --- | --- |
| Release `TTPlayerRebuild.exe` | `dd693d33cd9d94b8c6e1fc2da7455dbb2f5b77871406c17d34efe7a027e20447` |
| 集成 `ttpbrowser.dll` | `9a5b2d8c6e0a5fe8a75415cb63e65d137318801e05d0723ff0baa19fb7ad54e7` |
| 独立 `ttpbrowser.dll` | `9690011d0fad53646f066011c4be23c24712501e3118ebcb7f6d250dddd6faae` |
| `ttpbrowser-2026.10.27p2.zip` | `015019213a8845b356e1ff333afcc05fb180661191447a305ea09ad47ee33d52` |

## 音乐窗鼠标修正 / 2026-10-27

`2026.10.27p1` 修正外框右键、下载列表单击/双击、表头、提示、滚动、分隔条、底部链接轮播与 Shell 删除文件，并通过可选宿主回调接入明确关联的歌词。按用户要求不实现“每周推荐”列表；边框右键另提供“音乐窗”“下载管理”切换菜单。详细证据、行为与保留边界见 [MOUSE_BEHAVIOR_FIX.zh-CN.md](MOUSE_BEHAVIOR_FIX.zh-CN.md)。

独立及集成构建、CTest 5/5、宿主存在/缺失/不兼容、WebView2/IE/自动回退各 11/11、隔离启动退出检查均通过。启动退出码 0，捕获异常 0。原版只做静态分析，文件删除仅做模拟测试；未执行旧系统 VM 验证。DLL 为 464,384 字节，ZIP 为 216,529 字节；单 DLL 白名单、x86、校验和及 XP/Win7 导入审计通过。

已更新 `rebuild/build/Release` 中配套 EXE 和 DLL，完整歌词关联需要同时使用这两个文件。修改前备份、截图、日志及校验清单位于 `out/mouse-fix-20261027/`，产物为 `build/Release/ttpbrowser-2026.10.27p1.zip`，未发布远端发行。

## 九宫格拉伸修复 / 2026-10-22

`2026.10.22p4` 修复默认音乐窗未提供 `resize_rect` 时整图缩放的问题。像素级契约检查、默认资源边角检查、吸附回归、模块存在/缺失/不兼容宿主测试及隔离启动检查通过。详细行为、原版静态证据和验证边界见 [NINE_PATCH_FIX.zh-CN.md](NINE_PATCH_FIX.zh-CN.md)。

组件 454,144 字节，与 p3 相同；ZIP 210,243 字节。单 DLL 包白名单、x86、校验和及 XP/Win7 静态导入审计通过。已将新 DLL 部署到 `rebuild/build/Release`，主程序 EXE 与用户 XML 保持原哈希。日志、截图、备份及最终清单位于 `out/nine-patch-fix-20261022/`。本轮未发布远端发行。

## 音乐窗吸附修复 / 2026-10-22

版本 `2026.10.22p3` 为音乐窗移动/缩放增加可选宿主吸附回调。完整行为、原版证据及 ABI 兼容策略见 [SNAP_FIX.zh-CN.md](SNAP_FIX.zh-CN.md)。本次需要配套使用更新后的主程序。

- DLL 独立构建和主程序集成构建成功；EXE 与 DLL 静态导入审计通过。
- CTest 契约与下载测试 2/2 通过，包含旧宿主边界内存读取检查。
- 宿主的模块存在/缺失/不兼容三种测试通过；存在模式包含吸附边界、配置、面板、真实鼠标移动/缩放、取消、最小尺寸、主窗传递组及状态保存回归。
- WebView2、IE、Runtime 缺失时自动回退各 11/11 通过。
- Release 配套 EXE、核心和资源 DLL 与新组件在隔离目录启动/退出成功，退出码 0、异常 0。
- ZIP 单 DLL 白名单、x86 和校验和验证通过。组件 454,144 字节，比 p2 增加 512 字节；ZIP 210,184 字节。

| 文件 | SHA-256 |
| --- | --- |
| `ttpbrowser.dll` | `23a71ae04a383e709d754abcf1b326bdd2c9cc513a90f999f8aa1a2633a32542` |
| `TTPlayerRebuild.exe` | `2dab1aaa0358d5f5d2e1fa31fd5bf06e67119cf89d0263d56f8ae8a820ed274a` |
| `ttpbrowser-2026.10.22p3.zip` | `032f66d6372b9d932ff6bc5829533ba27b1dbc8b3777660459888c2df00990a4` |

部署目标为工作区 `rebuild/build/Release`，其中 EXE 与 DLL 均已更新。配置文件哈希保持一致。工作区根目录仍有旧核心/资源库；把新 EXE 与该组旧运行库混用的额外隔离探测超时，没有作为启动通过结果，因此本次不升级根目录的 EXE/DLL。请从 Release 目录运行修复版。

本地日志、原版证据复核和部署哈希在 `out/snap-fix-20261022/`。备份中 `deployment-before/workspace/` 是未改动的根目录文件；Release 的上一轮 EXE/DLL 从已有隔离验证副本按已知哈希恢复保存为 `TTPlayerRebuild.previous.exe` / `ttpbrowser.previous.dll`，同目录无 `previous` 后缀的 EXE/DLL 是本轮构建后的快照。未发布 GitHub Release，未执行旧系统虚拟机实测。

## Loader 合并与体积优化 / 2026-10-22

版本 `2026.10.22p2` 将 WebView2LoaderStatic.lib 链接进 ttpbrowser.dll，保留 Runtime 检测、自动 IE 回退和强制 WebView2 的错误处理。C ABI 不变。组件及主程序的构建流程不再复制或安装外部 Loader；Action 打包改为一个 DLL、第三方声明和 SHA256SUMS.txt。

Release 的大小优化现在同时作用于独立构建和主程序集成构建。使用 `/O1 /Ob2 /Os /Gy /Gw /GF /GR-`、LTCG、`/OPT:REF /OPT:ICF`，保留异常处理和安全检查。比较三档内联后，`/Ob2` 为 453,632 字节，`/Ob1` 为 454,656 字节，`/Ob0` 为 483,840 字节，因此保留 `/Ob2`。

| 比较对象 | DLL 本体 | 独立 Loader | 合计 |
| --- | ---: | ---: | ---: |
| 原部署文件 | 554,496 | 116,304 | 670,800 |
| 上轮独立 Action 构建 | 438,784 | 116,304 | 555,088 |
| 本次合并版本 | 453,632 | 0 | 453,632 |

单位为字节。本次 DLL 为 **443 KiB**，较原部署两文件合计减少 **32.4%**，较上轮 Action 包的两文件合计减少 **18.3%**。`Optimal` ZIP 压缩后的发行包为 **210,066 字节**。

验证结果：

- CTest 契约及下载测试 2/2 通过。
- WebView2、强制 IE、Runtime 不可用时的自动 IE 回退各 11/11 通过；每种均检查进程没有加载外部 Loader。隔离目录只包含测试宿主和合并后的 DLL。
- 主程序宿主测试的 DLL 存在、缺失、不兼容三种情况均通过。
- 复制现有 Release EXE 和运行库到隔离目录，使用新 DLL 的 `--smoke-test` 启动及退出通过，退出码 0、捕获异常 0，未加载独立 Loader。
- 独立及集成构建均通过 XP/Win7 静态导入审计：12 个 DLL、278 个导入；未在旧系统虚拟机运行。
- 包内外 SHA-256、三个文件白名单及 PE32/x86 验证通过；含旧 Loader 或被改动 DLL 的异常包被拒绝。PowerShell、Python 和 Action 语法检查通过。

产物：`build/Release/ttpbrowser-2026.10.22p2.zip`。本地详细记录为 `out/loader-merge-20261022/verification.json`，包含构建、测试、审计和部署记录；该目录被 Git 忽略。

| 文件 | SHA-256 |
| --- | --- |
| `ttpbrowser.dll` | `ee8ff30bef721e08709f0fed8cbc6e8663f1544e5efe1d9757cc08e31f82ef06` |
| `ttpbrowser-2026.10.22p2.zip` | `3a471b61f90219fd88e69fe478cc08e0bbde7394dafe938c4b48e21282bb78e6` |

已将同一新 DLL 部署到工作区根目录和 `rebuild/build/Release`，附组件第三方声明。原 DLL 和独立 Loader 保存到 `out/loader-merge-20261022/deployment-before`，用户配置保持原字节内容。正在运行的播放器未被终止，重新启动后使用新 DLL。未发布 GitHub Release。

以下保留合并前验证记录，用于追溯旧包布局和哈希。

## 合并前的独立构建与 Action 验证 / 2026-10-22

本记录对应新增的独立构建与发布流程。原音乐窗功能、闪退和界面修复的历史验证仍保留在各自文档中。

### 隔离构建

将源码、构建脚本、依赖快照和文档复制到 `out/ci-validation/source`，使用新的 `out/ci-validation/build` 构建目录；该源码目录旁没有 `rebuild` 主程序项目。未指定本机 WebView2 SDK 或 FetchContent 依赖路径，由 CMake 下载并校验固定版本依赖。

```powershell
./build.ps1 -BuildDirectory <隔离构建目录> -Package -PackageVersion 2026.10.22p1
```

MSVC Win32 Release 构建、版本资源验证、安装和 ZIP 打包全部成功。公共源码快照的 11 个文件与工作区原文件及清单 SHA-256 一致。主程序现有构建目录重新配置也成功，保留集成构建入口。

### 检查结果

| 检查 | 结果 |
| --- | --- |
| PowerShell 脚本语法 | 构建与所有 `cmake/*.ps1` 通过 |
| Action YAML、表达式与任务依赖 | actionlint 1.7.12 通过 |
| 日期版本及 DLL 版本 | 11 项通过，包含非法日期、补丁上限、同日标签和草稿发行冲突 |
| 发布准备 | 离线校验 ZIP 并生成发行说明成功，未调用发布操作 |
| 运行包 | 四个成员白名单、内部与外部 SHA-256、两个 DLL 的 x86 架构通过 |
| 异常运行包 | 内容被改动、额外成员、x64 DLL 三种情况均被拒绝 |
| DLL 旧系统静态导入 | XP/Win7 清单通过，11 个 DLL、269 个导入，子系统 5.01 |
| 本地显式开启测试 | CTest 契约与下载测试 2/2 通过 |
| 真实浏览器后端 | WebView2 10/10、IE/MSHTML 10/10 通过，仅访问本机测试页面 |

actionlint 来自官方 v1.7.12 发行，下载后按发行校验清单验证。自动工作流仍只构建、审计和打包；上表运行测试是本次本地验证单独开启的。

### 产物与记录

验证包：`out/ci-validation/build/Release/ttpbrowser-2026.10.22p1.zip`。版本字符串为 `2026.10.22p1`，Windows 固定文件与产品版本均为 `2026.10.22.1`。

| 文件 | SHA-256 |
| --- | --- |
| `ttpbrowser-2026.10.22p1.zip` | `c997cfbf411256619df24294b8902f8a967767e94297c412a2ef2a8c3404330e` |
| ZIP 内 `ttpbrowser.dll` | `4c5405ca1529f4ecd128c5b213e9833db05973cb0ad46cb4a2d11c15714f2069` |

本地日志保存在 `out/ci-validation/`：`build.log`、`actionlint.log`、`release-validation.log`、`tests-build.log`、`ctest.log`、`runtime.log`、`host-configure.log`。这些路径以组件根目录为基准，被 Git 忽略，不随运行包发行。

未在 GitHub 云端运行工作流或发布 Release，未执行旧系统虚拟机验证。本次验证包留在隔离输出目录，没有覆盖用户已部署的主程序和 DLL。
