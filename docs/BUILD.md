# 构建与 GitHub Actions

## 本地构建

在 `ttpbrowser` 目录运行 `./build.ps1 -Package`。要求 MSVC x86 编译器及 ATL、Windows SDK、CMake 3.24+、Python 3、PowerShell。默认生成器为 `Visual Studio 18 2026`，可通过 `-Generator` 修改；额外 CMake 参数使用 `-CMakeArguments` 数组传入。

```powershell
./build.ps1 -Package -PackageVersion 2026.10.22p1
./build.ps1 -BuildDirectory out/local-build -CMakeArguments @('-DTTPBROWSER_WEBVIEW2_SDK=C:/sdk/webview2')
```

未指定 WebView2 SDK 时，CMake 自动下载固定版本的 NuGet 包并验证 SHA-256。离线构建可指定已解包 SDK，并使用 CMake 的 `FETCHCONTENT_SOURCE_DIR_*` 参数指向已取得的运行库和 WTL 源码。

独立构建使用 [support/rebuild](../support/rebuild/) 中的最小公共源码快照，不需要检出主程序。快照的每个文件由 [SNAPSHOT.json](../support/rebuild/SNAPSHOT.json) 记录来源和哈希，配置时检查完整性。嵌入主项目构建时仍使用主项目的公共源码，保持原有接入方式。

## 固定依赖

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| Microsoft.Web.WebView2 | 1.0.3179.45 | 原生头文件及 x86 静态 Loader |
| WTL | 10.01 | 宿主头文件和 ATL 配置 |
| VC-LTL | 5.3.1 | 旧系统 CRT 适配 |
| YY-Thunks | 1.2.2 | 新 API 的旧系统兼容入口 |

下载地址和 SHA-256 固定在 [webview2.cmake](../cmake/webview2.cmake)、[wtl.cmake](../cmake/wtl.cmake) 和 [xp_runtime.cmake](../cmake/xp_runtime.cmake)。构建后检查 `ttpbrowser.dll` 的 PE32/x86、子系统版本与 XP/Win7 导入表。WebView2 Loader 静态链接进该 DLL，与组件一并接受旧系统导入审计；Runtime 不可用时由浏览器后端回退到 IE/MSHTML。静态审计不替代旧系统运行测试。

WebView2 Loader 与 Runtime 是不同组件，见 [Microsoft 官方分发说明](https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution)。依赖声明位于 [licenses](licenses/)。

## 单 DLL 与体积优化

组件链接 SDK 的 `WebView2LoaderStatic.lib` 和系统 `version.lib`，直接调用 Runtime 检测及创建入口。Loader 不再作为独立文件加载或打包；Runtime 检测失败、初始化失败时保留自动 IE 回退，强制 WebView2 模式则显示错误。

Release 对组件自身启用 `/O1 /Ob2 /Os /Gy /Gw /GF /GR-`、LTCG、`/OPT:REF /OPT:ICF`，并关闭增量链接和调试信息输出。该设置同时适用于独立与主程序集成构建，不改变主程序的音频编译策略。保留异常处理和编译器安全检查。ZIP 使用 `Optimal` 压缩。

本次比较 `/Ob2`、`/Ob1`、`/Ob0` 的结果后保留 `/Ob2`。如需自行复测，可传 `-CMakeArguments @('-DTTPBROWSER_INLINE_LEVEL=1')`；体积与验证结果见下方验证记录。构建脚本不会删除旧安装目录里的文件，旧 `WebView2Loader.dll` 已不再被本组件使用。

## 手动 Action 与发行

[.github/workflows/build.yml](../.github/workflows/build.yml) 沿用其它插件的手动流程：

1. `workflow_dispatch` 触发，`Release a Version (GitHub)` 默认关闭。
2. 在编译前分配北京时间日期版本，发行时查询标签和发行记录，避免重复使用版本。
3. 使用 `windows-2025-vs2026`、Win32 Release 构建；不构建或运行测试。
4. 校验 DLL 版本和系统导入，生成 ZIP 及校验和，上传保留 14 天的 artifact。
5. 仅显式勾选发行时，独立 job 验证下载的 ZIP、内部文件哈希与架构，再用 `gh release create` 发布；已存在的发行不会被覆盖。

构建 job 只有 `contents: read`，发行 job 才获得 `contents: write`。Action 版本以完整提交 SHA 固定，检出不保留凭据。发行运行使用同一 concurrency group，直到发布完成；普通构建互不阻塞。运行器依据见 [GitHub 官方迁移公告](https://github.blog/changelog/2026-05-14-github-actions-upcoming-image-migrations/)。

生成工作流不会自动上传仓库、触发云端运行或发布发行。历次本地执行结果见 [BUILD_VALIDATION.md](BUILD_VALIDATION.md)。

## 安装包

`build/Release/ttpbrowser-<版本>.zip` 只有以下三个文件：

```text
ttpbrowser.dll
THIRD_PARTY_NOTICES.md
SHA256SUMS.txt
```

将 ttpbrowser.dll 放在播放器 EXE 同一目录，保留第三方声明。外部 `build/Release/SHA256SUMS.txt` 验证 ZIP，包内校验和验证两个载荷文件。`verify_package.py` 还检查成员白名单和DLL 的 x86 架构。

## 本地测试

直接配置 CMake 并显式开启测试；`build.ps1` 与发行 Action 保持构建产物的范围，不运行测试。

```powershell
cmake -S . -B out/tests -A Win32 -DTTPBROWSER_BUILD_TESTS=ON
cmake --build out/tests --config Release --target ttpbrowser_contract_tests ttpbrowser_download_tests ttpbrowser_runtime_tests
ctest --test-dir out/tests -C Release --output-on-failure
python tests/run_runtime_tests.py out/tests/Release --output out/runtime-check
```

浏览器测试要求新的输出目录，会只复制测试宿主和 ttpbrowser.dll 到隔离目录，并验证 WebView2、强制 IE、Runtime 不可用时的自动 IE 回退，以及进程未加载独立 Loader DLL。

真实 `PlayerWindow` 宿主测试还需要完整主项目，命令见 [实现说明](README.zh-CN.md)。原有 `out`、`reverse` 分析记录只留在本地工作区，不随独立源码或运行包发行。
