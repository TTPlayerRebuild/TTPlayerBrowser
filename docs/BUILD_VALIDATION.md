# 构建与发行验证记录

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
