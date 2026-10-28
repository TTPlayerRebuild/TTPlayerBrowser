# 构建与发行验证记录

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
