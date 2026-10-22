# Release 启动闪退修复 / 2026-10-22

用户报告路径：
D:\Projects\Backup\TTPlayer\rebuild\build\Release\TTPlayerRebuild.exe

## 根因与证据

在用户指定的 EXE 和配置上，用 Windows Debug API 启动并捕获异常：

1. COMCTL32.dll + 0xB86E3 首次异常 0xC0000005，访问类型是写入。

2. 目标是旧 ttpbrowser.dll + 0x44670 的 .rdata 只读区，内容为“关闭”提示字符串。

3. 随后是未处理的窗口回调异常 0xC000041D，进程退出。
原始异常和模块记录：../out/startup-crash-20261022/before.json。

browser_window.cpp 的 UpdateTooltips 在判断工具是否已注册时发送 TTM_GETTOOLINFOW，
却把 TOOLINFO.lpszText 指向 kLabels[i] 只读字符串。该消息把 lpszText 当输出缓冲区。
真实主程序有 Common-Controls 6.0 manifest，系统控件写回提示内容时触发访问异常。
此前测试 EXE 没有相同 manifest，使用的控件实现不同，因此旧测试结果没有覆盖这一条件。
这个问题与原网站是否可用、是否安装 WebView2 无关，发生在原生音乐窗布局阶段。

## 修复

UpdateTooltips 的查询结构保持 lpszText=nullptr，只查询注册信息；只有 TTM_ADDTOOLW
注册工具时才传入标签作为输入。后续布局通过 TTM_NEWTOOLRECTW 更新位置。
没有通过关闭音乐窗自启动、隐藏异常或禁用功能规避问题。

测试程序均使用与正式 EXE 相同的 Common-Controls v6 manifest。
契约测试明确断言控件主版本>=6，并验证：工具数量、可写缓冲区读取提示文字、
多次 resize/configure 后提示内容不变、位置正确更新、没有重复注册。
测试先对未修复 DLL 运行，成功复现同一个退出码；修复后通过。

## 验证结果

1. 指定 Release EXE 正常可见启动，运行8秒，主窗口、播放列表、歌词窗和音乐窗均存在；
   发 WM_CLOSE 后退出码0，未捕获访问异常或未处理异常。
   记录：../out/startup-crash-20261022/after-normal.json。

2. 同一 EXE --smoke-test 启动/退出通过，退出码0，未捕获异常。
   记录：../out/startup-crash-20261022/after-smoke.json。

3. v6控件契约测试、下载测试 2/2 suites通过。
   记录：../out/startup-crash-20261022/ctest-fixed.log、contract-download-fixed.log。

4. WebView2和IE实际页面测试各10/10通过，使用v6控件环境。
   记录：../out/startup-crash-20261022/runtime-v6/。

5. DLL静态XP/Win7导入审计通过，12 DLL/238 imports；主EXE构建导入审计也通过。
   旧系统VM运行不属于本次验证范围。

## 部署

指定 Release 目录中的 TTPlayerRebuild.exe 已重新构建，ttpbrowser.dll 已更新。
工作区根目录的 ttpbrowser.dll 也同步修复，以免另一个已部署副本保留同一缺陷。
旧根目录DLL备份：../out/startup-crash-20261022/ttpbrowser-before-fix.dll。
启动验证结束后，Release/TTPlayerRebuild.xml 已恢复测试前内容。

修复DLL SHA256：
16BE90775BA5179B3585F5B7868EA5755A12A9E5155FE9862372EFA7B4573438
Release EXE SHA256：
7ABB4349E0CD92EF4DB2C29D38790AEA01E8D93B919F9EF2EEEAC7C7C4CD4338

Windows消息契约参考：
[https://learn.microsoft.com/en-us/windows/win32/controls/ttm-gettoolinfo](https://learn.microsoft.com/en-us/windows/win32/controls/ttm-gettoolinfo)
