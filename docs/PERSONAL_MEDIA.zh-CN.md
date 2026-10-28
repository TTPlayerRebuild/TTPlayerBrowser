# 个人媒体库：OpenSubsonic 与 OpenList

实现版本：`2026.10.28p3`，协议与宿主接入始于 p2。设置页依赖已完成 2026.10.28 接入的主程序；本次更新包含 `ttpbrowser.dll` 和 `AddIn/ttp_https.dll`，不需要更换 EXE。Jellyfin 不在本次范围内。

## 使用

1. 启动更新后的主程序，打开音乐窗。默认历史首页配置现在进入个人媒体库；已有自定义网页首页继续打开原网页。
2. 在 **选项 → 网络连接 → 音乐窗 → 媒体来源** 管理来源列表。列表仅显示 **名称、类型** 两列；右侧图标支持添加、删除、上移、下移和编辑；复用现有选项的原尺寸位图、15×14 DLU 按钮、连续排列和主题/禁用状态。双击行或点击编辑图标打开独立编辑窗口，可设置名称、类型（**OpenSubsonic / Navidrome** 或 **OpenList**）、地址、认证方式和凭据，支持测试连接、确定和取消。音乐窗右下角齿轮弹出菜单，第一项 **设置** 打开此列表，后续按列表顺序显示来源名称；勾号表示当前来源，点击名称立即切换。网络连接的 **连接设置** 页保留代理服务器、MusicBrainz；**音乐窗** 页保留原来的 **在线试听**、**歌曲下载** 两个子页。
3. 点击专辑、歌手、歌单卡片或目录继续浏览；勾选歌曲后使用底部右侧的 **播放**、**添加**、**下载**；每行右侧的三个图标仅操作该行歌曲，不改变勾选状态。点击歌曲名称查看详情，双击播放；支持多选、全选、分页和 Enter 搜索。
4. 下载继续使用原有下载管理。未设置保存目录时，会打开主程序下载设置；选择目录后再次下载。
5. 在音乐窗皮肤空白或边框右键，可切换 **个人媒体库** 与 **音乐网页**。原 `mini.qianqian.com` 入口仍保留，原网站服务是否可用由远端决定。

个人媒体库默认使用 DLL 内嵌的 HTML/CSS/JavaScript 页面，通过 WebView2 显示。页面无需部署网站，不使用外部框架、CDN 或本地 HTTP 服务器；HTML 与其它代码一起编入单个 DLL。专辑采用卡片，歌曲标题和元数据各占一行，超长文本省略，OpenList 只显示文件名和大小，选中歌曲后才展开详情。

页面去掉品牌标题、顶部来源栏和底部固定提示。搜索与分类保留，来源切换位于齿轮菜单，选择后立即刷新音乐窗并持久保存。底部左侧为紧凑页码/前后翻页，右侧为批量操作，最右侧为设置齿轮。页面根据实际浏览结果逐步显示可用页码（最多五个），不会预设不存在的页数；没有选择歌曲时批量按钮禁用。

网格封面右下角的播放图标直接收集专辑、歌单或歌手的歌曲并提交主程序，保留当前网格；封面其它区域及标题仍进入内容。收集按顺序读取所有页、按歌曲 ID 去重，限制为 1000 首、64 次列表请求和 30 秒；超限或失败时不提交不完整列表，提示用户打开后分批操作。切换页面或来源会取消未完成的收集。

播放交给主程序，下载交给现有 DLL 队列；这次没有改动音频解码和下载管理界面。个人媒体库按 **WebView2 → IE / MSHTML → 原生列表** 回退；指定 IE 时从 IE 开始。Runtime 不可用、初始化失败、进程失败或启动超时会尝试 IE，IE 控件或脚本不可用才回退原生列表。历史音乐网页继续沿用原有 WebView2 / IE 后端。

## 网页与原生接口

`src/media_page.html` 作为 RCDATA 编入 DLL，`src/media_web_view.cpp` 在 `https://library.ttpbrowser.invalid/index.html` 拦截并返回该资源；此域名不会作为服务地址访问网络。新页面不获取服务密码，也不直接请求 Navidrome / OpenList。

页面通过 WebView2 的消息接口调用白名单操作：读取来源名称/类型/标识、选择来源（selectSource）、查询列表、封面/歌词、打开主程序选项、容器播放（playContainer）、单曲/多选播放、加入列表和下载。耗时请求在原生 worker 运行，UI 线程回传结果。切换来源使旧列表令牌失效，播放和下载只接受当前列表中的索引，不接受网页传入任意 URL。关闭和回退会取消请求并等待 worker 结束。

来源列表由 DLL 的原生子窗口承载在选项页，凭据表单位于其独立模态编辑窗口，密码用掩码显示，网页不接触密码输入。编辑窗口点确定后保存，取消丢弃编辑；列表删除、排序立即保存。保存失败保留原配置并显示错误，成功后通知已打开的音乐窗刷新。列表选择只用于编辑，不切换播放来源。支持多个同类型服务，最多 64 个来源；允许清空列表，此时音乐窗提示从齿轮菜单添加来源。网页缓存位于 `HttpDownloads/PersonalLibrary.WebView2`。

网页仅允许内置地址导航；外部请求、弹窗、frame、浏览器下载和权限请求受到限制，网页内容通过 textContent 渲染。封面仅接受受限位图数据 URI。历史网页脚本桥与此消息接口分别管理。参考 [Microsoft WebView2 安全指引](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security) 和 [内置内容加载方式](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content)。

IE 使用 DLL 内的 `media_page_ie.html`（HTML 资源 302），采用 ES5 和 `window.external.send`，与 WebView2 共用受限消息处理及后台请求。只接受该 DLL 的固定 `res:` 文档，不接受外部导航或 frame。IE 的 ActiveDocument 下载事件允许加载内置 HTML；普通下载仍由 DLL 队列处理。参考 [Microsoft res 协议](https://learn.microsoft.com/en-us/previous-versions/aa767740(v=vs.85))。

对旧 GBK 标签被服务端误读成 Latin-1 的情况，在显示层做严格可逆修正，例如 `Ò»Á±ÓÄÃÎ` → **一帘幽梦**、`ÉÏº£Ì²` → **上海滩**，同时处理歌手、专辑、专辑/歌手页标题。修正要求连续高字节、有效 GBK、至少两个汉字及完全相同的编码往返结果；正常中文、日文、西文重音字符保留。仅处理 OpenSubsonic 展示元数据，不更改 ID、路径、音频标签、数据库或服务端专辑分组。搜索仍遵循服务端索引。

## 服务配置

### Navidrome / OpenSubsonic

- 地址可填 `http://127.0.0.1:4533`，也支持反向代理子路径、以 `/rest` 或 `/@rest` 结尾的 API 根路径。
- 通常选择 **密码登录**，填写服务端用户名和密码。请求采用 Subsonic 的随机 salt + MD5 token 认证，不把明文密码写入 URL。
- 服务端支持 OpenSubsonic API Key 扩展时可选择 **OpenSubsonic API 密钥**。此模式不发送用户名和密码认证参数。
- 已实现最新专辑、歌手、歌单内容、全部歌曲、收藏歌曲、歌曲搜索、封面及歌词预览。歌词按扩展能力调用 `getLyricsBySongId`，不支持时尝试 `getLyrics`。
- 播放请求使用 `stream` 的原始格式；下载使用 `download`。播放格式仍取决于主程序已安装的解码插件。本次未提供转码码率选择、收藏修改、远端歌单编辑或播放统计回传。

### OpenList

- 地址填写 OpenList 站点根地址，支持部署子路径；不要填写 `/dav`、`/api/fs/list` 或文件分享链接。
- 支持密码登录、已有登录令牌和访客访问。启用两步验证的账号可以使用已有登录令牌；当前界面不负责获取动态验证码。
- **起始目录** 是当前账号可见目录内的路径，例如 `/` 或 `/Music`。**目录密码** 与账号密码分开填写。
- 默认启用 **通过 OpenList 服务器代理音频**，采用 `/p` 路由，让 OpenList 处理底层存储需要的请求头与 Cookie。请在对应存储启用 Web 代理或其支持的代理配置。
- 关闭代理时使用 `fs/get` 返回的直链；若存储要求额外请求头，客户端会要求启用代理，而不会静默丢弃这些请求头。
- 支持目录、音频文件筛选、分页及服务端搜索。搜索需要服务端建立索引并赋予账号相应权限；没有索引仍可浏览目录。
- 获取文件地址前重新请求 `fs/get`，处理签名变化；密码登录的令牌过期时重登录一次。受限账号的 `base_path` 会映射到服务器代理路由的真实路径。
- 当前适配使用 OpenList 文件 API，不依赖其媒体库测试版或 WebDAV，也不提供文件上传、删除、移动功能。

## 持久化与宿主接入

文件位于与下载队列相同的设置目录中：

- `HttpDownloads/MediaLibrary.dat`：Windows DPAPI 加密的来源配置、当前来源选择和凭据，绑定当前 Windows 用户；复制到另一台机器或另一用户时需要重新配置。
- `HttpDownloads/PersonalLibrary.enabled`：个人媒体库与网页的默认页面选择。
- `HttpDownloads/cachelist.xml`：原有下载队列。

播放列表与下载队列保存形如 `https://ttpbrowser.invalid/v1/<来源标识>/<play或download>/<歌曲ID或路径>/audio.flac` 的内部标识。这是播放器内部地址，不是外部网站。来源标识包括独立 ID、服务器与账号指纹。改名和排序保持 ID，旧播放列表仍有效；更换类型生成新 ID，修改服务器/账号会改变指纹，旧曲目不会被解释为新服务里的同名歌曲。删除来源后，旧播放列表项目不会自动改绑到其他来源。

来源配置升级为带名称、类型、独立 ID 和顺序的版本 2 格式，当前来源按 ID 保存。旧版两条固定来源自动读取并保留原有 ID、凭据和选中项，首次修改后写入新格式。删除当前来源时选择相邻可用来源；清空后重启仍保持空列表。

主程序在 HTTP producer 线程调用 DLL 的可选 `resolve_source` 接口，将标识解析为当前有效 URL。初次打开及后续 Range 请求都会解析；解析失败不会把内部地址发送到 DNS。已有 HTTP 缓冲、Range 校验、解码器和下载 worker 保持工作。此类流不写入跨会话 HTTP 缓存，完整下载文件仍正常保存。

DLL 的个人媒体库登录、列表、搜索、封面、歌词和地址解析请求优先加载 EXE 同目录的 `AddIn/ttp_https.dll` ABI 8，支持 HTTP/HTTPS GET 与 JSON POST。组件缺失、损坏、接口不足或明确要求系统代理处理时回退 WinINet；证书失败、超时、取消及发送后的错误不会重试。两条路径均保持证书验证，不共享浏览器 Cookie，不跟随认证请求重定向，服务地址应填写最终地址。请求限制为 1 MiB 正文、8 MiB 响应和 20 秒总期限。

服务 API 使用系统代理；音频流仍通过主程序的 `ttp_https` ABI 7，使用主程序网络设置，两者的代理设置可能不同。历史 mini.qianqian.com 页面自身的请求由浏览器引擎处理。WebView2 → IE → 原生属于界面回退，与传输回退独立。

ABI 版本仍为 1，使用 `size` 检测尾部扩展。新增 `open_media_settings` 宿主回调和 `create_settings_page` / `commit_settings_page` DLL 接口；旧 DLL 缺少此扩展时不创建媒体来源页。新主程序能加载旧 DLL；旧主程序加载新 DLL 时保留原网页及下载功能，不显示需要新解析器的个人媒体入口。卸载前撤销解析器并等待正在进行的有界请求，避免回调进入已销毁实例。

## 本机验证

本次按用户指定目录 `C:\Users\Sonic853\Music\test` 建立独立测试配置，未修改该目录中的音乐文件：

- Navidrome：使用 Scoop 安装的 `0.64.2`，监听 `http://127.0.0.1:45331`，扫描到 10 首可识别歌曲。
- OpenList：测试版本 `4.2.9`，监听 `http://127.0.0.1:52441`，挂载相同目录；客户端使用限制到该目录的只读测试账号，已建立本地搜索索引。
- 数据库、日志、测试凭据清单与服务配置都在忽略目录 `out/media-integration/live2`。测试凭据未纳入源码或发行包，也没有添加系统服务或开机启动。
- 最初接入时曾在 Release 配置中预填这两个测试来源，后续升级保留用户当前列表。2026.10.28p3 部署前列表已为空，本次部署保留该状态。重启 Windows 后可在 PowerShell 中运行本地脚本 `ttpbrowser/out/media-integration/start-test-services.ps1` 重新启动这两个实例，再在音乐窗设置中添加来源，或使用自己的常驻服务。该脚本和测试数据保存在本机忽略目录，不随发行包分发。

测试工具：`tests/run_media_services.py` 建立隔离服务，`ttpbrowser_media_tests --live <测试清单> <队列路径>` 验证真实 API 并写入加密客户端配置，`ttpbrowser_media_host_tests <隔离运行目录>` 验证原生回退、播放流、解码和下载。`tests/run_media_web_tests.cjs` 使用 Playwright/CDP 验证内嵌页面，调试端口只对隔离测试进程启用，不用于正式播放器。测试清单包含随机测试密码，不应提交或公开。

本次来源列表和菜单证据位于 `out/media-sources-20261028p2`，覆盖原生编辑器的增删改排序/取消/失败、多个同类型服务、菜单顺序/勾选/键盘和三引擎来源切换。此前紧凑布局证据位于 `out/media-compact-20261028p1`：真实 WebView2、IE、原生回退均通过；网页覆盖 320/440/536/680 像素宽度、封面播放、单曲三按钮和选项页切换来源。其余验证证据位于 `out/media-integration` 和 `out/media-*.log`，包括协议/异常/编码/持久化测试、真实服务查询、原生流读取和 Seek、FLAC/M4A 解码、下载原文件哈希比对、旧网页桥和宿主兼容回归。p3 在 XP SP3 / Win7 SP1 实测原生来源编辑器、HTTP/HTTPS 传输、宿主创建/关闭和正式 EXE 隔离启动；不是完整旧系统服务功能的穷举验证。

2026.10.27p3 的早期网页截图和验证位于 `out/media-web-20261027`，覆盖 320/440/680 像素宽度、设置表单、来源快速切换、无凭据读取、过期列表拒绝、外部导航/网络限制、WebView2 缺失及初始化失败回退。

## 协议依据

- [OpenSubsonic API](https://opensubsonic.netlify.app/docs/opensubsonic-api/)
- [OpenSubsonic 认证与响应约定](https://opensubsonic.netlify.app/docs/api-reference/)
- [OpenList 路由](https://github.com/OpenListTeam/OpenList/blob/v4.2.9/server/router.go)
- [OpenList 文件 API](https://github.com/OpenListTeam/OpenList/blob/v4.2.9/server/handles/fsread.go)
- [OpenList 登录](https://github.com/OpenListTeam/OpenList/blob/v4.2.9/server/handles/auth.go)
- [OpenList 下载与代理](https://github.com/OpenListTeam/OpenList/blob/v4.2.9/server/handles/down.go)

p3 的绘制/XP 兼容记录位于 `out/media-source-paint-20261028p3`；ABI 8 传输矩阵、TLS 私有夹具和实际媒体库页面记录位于 `out/https-20261028p3`。测试工具为 `tests/run_https_transport_tests.py`。
