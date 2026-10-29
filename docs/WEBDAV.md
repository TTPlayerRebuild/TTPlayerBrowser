# WebDAV 接入与验证（2026.10.29）

配套版本：`TTPlayerRebuild.exe` / `TTPlayer.exe`、`ttpbrowser.dll`、`AddIn/ttp_https.dll` 均为 `2026.10.29`。主程序需要一并更新，才能把 WebDAV 的认证信息传给播放和下载流。

## 配置与使用

进入 **选项 → 网络连接 → 音乐窗 → 媒体来源**，添加来源并选择 **WebDAV**。

- **服务器地址**：填写最终 WebDAV 挂载地址，例如 `https://example.org/dav`。Nextcloud 等服务应填写其实际 WebDAV 用户目录入口；不是站点首页，也不是单个音频的分享链接。
- **认证方式**：用户名密码（Basic）或匿名访问。当前没有 Digest、OAuth、客户端证书登录。
- **起始目录**：相对于上述挂载地址的目录，例如 `/`、`/Music`。中文直接填写，路径不允许 `..`。
- 点击 **测试连接** 验证认证和目录，再保存。右下角齿轮菜单按原有来源顺序显示、切换 WebDAV 来源。

支持进入目录、返回、多选、100 项分页、当前目录名称筛选，以及播放、加入播放列表、下载。播放列表显示文件名，下载保留文件名及扩展名；WebDAV 不提供 OpenSubsonic 的专辑索引、歌手、歌词或封面。播放格式取决于主程序的解码插件。下载使用原来的下载管理与重名处理规则。

起始目录限定客户端浏览范围；服务端仍负责权限控制。不提供上传、删除、移动、改名或远程递归索引。

## 实现与回退

`src/media_webdav.inl` 发起 `PROPFIND Depth: 1`，解析 `DAV:` 命名空间的 `207 Multi-Status`，识别 collection 和成功的 propstat，按目录优先排序并筛选音频。支持绝对、根相对与目录相对 href；中文、空格、`#`、`%`、`+` 按 UTF-8 编解码，不把显示名称当作资源路径。忽略不同来源、上级路径、非直接子项和无权限条目。

XML 使用 MSXML 6，缺少时回退 MSXML 3；禁用 DTD、外部实体、外部解析与验证。响应上限 8 MiB、目录上限 10,000 项。前端沿用 **WebView2 → IE → 原生列表**，三种界面共用同一原生媒体库实现。

传输层新增 `ttp_https` ABI 9：

| 操作 | 首选 | 回退 |
| --- | --- | --- |
| 目录和连接测试 | ABI 9 `stream_open_ex2`，PROPFIND + Depth + Authorization | WinINet |
| 需要认证的播放、下载和定位 | ABI 9，GET + Authorization + Range / If-Range | WinINet |
| 匿名音频流 | 既有 ABI 7 | WinINet |
| OpenSubsonic / OpenList API | 既有 ABI 8 | 原有 WinINet 路径 |

仅组件缺失、无法加载、接口不足或组件明确返回原生处理信号时回退。证书、HTTP 状态或读取失败不会通过切换传输重复发送。HTTPS 不降为 HTTP；带认证的音频重定向不能跨 origin。目录接口返回重定向时提示填写最终地址。调用方显式提供私有 CA 或独立 PAC 策略时，系统回退会拒绝无法保留的策略，不会静默改用系统信任或代理。系统回退使用 Windows 自身 TLS 能力，XP/Win7 访问现代 HTTPS 应保留新版 `ttp_https.dll`。

目录请求沿用系统代理；播放、下载沿用主程序网络设置。两者不共享浏览器 Cookie。新版宿主接口 `resolve_source_ex` 分别返回 URL 和临时 Authorization，认证在初次请求和后续定位时重新解析。密码只保存在当前用户 DPAPI 加密配置中，网页、播放列表、下载队列均不接收认证头。旧 `resolve_source` 遇到需要认证的 WebDAV 会明确要求更新主程序，不会丢弃认证继续请求。

ABI 1～8 的结构与行为保持兼容；浏览器接口仍使用原 ABI 版本和尾部 size 检测。原有来源配置、来源 ID、排序和当前选择无需迁移。

## 实测记录

测试服务为独立 **WsgiDAV 4.4.0 + Cheroot 11.1.2**，HTTP 端口 55479、HTTPS 端口 55480，随机 Basic 测试账号及匿名挂载。样本完全在忽略目录 `out/webdav-20261029` 中生成：中文子目录、带 `#%+` 的中文 WAV 文件、普通音频和应过滤的文本文件。没有修改用户音乐目录、实际媒体来源或系统证书库。HTTPS 测试 CA 只由测试调用方显式传入。

| 环境 | 实测结果 |
| --- | --- |
| XP SP3 x86 / IE8 | 目录、中文路径、当前目录搜索、播放/添加、下载、设置、原生列表回退全部通过 |
| Win7 SP1 x64 / IE11 | 同上，全部通过 |
| XP、Win7 / 新版 ttp_https | 真实 HTTPS PROPFIND、认证 Range、原文件字节比较通过；未受信证书被拒绝 |
| XP、Win7 / 缺少或旧版 ttp_https | HTTP 目录、IE 页面、原生播放解码、定位和下载通过系统网络回退 |
| 本机 / WebView2 | 实际内嵌页面的目录、搜索、播放/添加、下载、选项入口及无脚本错误检查通过 |
| XP、Win7 / 正式 EXE | 新版 `TTPlayerRebuild.exe --smoke-test` 均退出 0 |

每个旧系统的协议单元检查 88 项、真实目录检查 6 项、IE/宿主检查 26 项、设置检查 32 项、原生界面/宿主检查 23 项均无失败。TLS 探针还验证 ABI 1～8 表仍可取得、Depth/Authorization 注入拒绝、取消与 ABI 8 方法限制；其中逐块读取断言不是独立场景。缺少/旧组件两组各通过 6 项目录和 28 项宿主检查。

下载文件按完整字节与原 WAV 比较；使用主程序原生解码器产生 PCM，并测试 30 秒解码定位及 10,000,000 字节流定位。服务日志确认后续 Range 请求携带认证。旧 ABI 8 的 HTTP、TLS 1.2/1.3、取消、超时、错误及回退回归为 **19/19 通过**；DLL 契约检查为 0 失败。静态 XP/Win7 导入检查和两份发行 ZIP 检查通过。

本次没有重跑 Sayobot 或所有历史媒体库功能，也没有在 Win7 的 WebView2 上重跑本次 WebDAV 页面；Win7 本次页面证据来自 IE11 和原生列表。

测试代码：`tests/webdav_library_tests.inl`、`tests/webdav_host_tests.inl`、`tests/https_webdav_tests.inl`、`tests/serve_webdav_fixture.py`、`tests/run_webdav_web_tests.cjs`，按仓库惯例保存在本地忽略的 tests 目录。日志、各系统截图、私有测试清单与打包日志在 `out/webdav-20261029`；私有清单及测试 CA 不随包发行。

协议依据：[RFC 4918 / WebDAV](https://www.rfc-editor.org/rfc/rfc4918.html)。
