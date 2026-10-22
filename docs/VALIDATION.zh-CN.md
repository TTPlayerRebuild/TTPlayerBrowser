# 音乐窗交付验证 / 2026-10-22

后续修复：本记录对应首次交付。随后发现正式EXE的Common-Controls v6环境会触发
提示文字查询的只读内存写入，已修复并在指定Release EXE上复验。
该启动问题的复现和回归见 [STARTUP_FIX.zh-CN.md](STARTUP_FIX.zh-CN.md)。随后外围窗框及下载页修复见
[UI_FIX.zh-CN.md](UI_FIX.zh-CN.md)；最新截图、测试与部署哈希位于 ../out/ui-fix-20261022/。

构建：MSVC Win32 Release，cmake --build rebuild/build --config Release。
目标：ttpbrowser_runtime_tests、ttpbrowser_contract_tests、ttpbrowser_download_tests、
ttpbrowser_host_tests、ttplayer_rebuild，全部成功；最终日志无编译 warning/error。
日志：build-final.log。

1. DLL 契约：34 项断言，全部通过。
   版本导出、非法ABI/host拒绝、6个脚本别名、查询、参数数量、批量提交、Tid、
   下载字节一致性、非HTTP源拒绝、下载页、隐藏、持久化、销毁后COM安全拒绝、重开。

2. 下载模块：通过。
   取消/重试、持久化、存储租约冲突、错误流/异常、FIFO/预取、导入、并发调整、
   HANDLE寿命、关闭join、下载子窗销毁重建。
   ctest：2/2 suites，0 failed；完整结果在 test-results.log 和
   ../../rebuild/build/ttpbrowser/Testing/Temporary/LastTest.log。

3. 实际浏览器页面：WebView2 10/10，IE 10/10。
   本地HTTP fixture真实脚本调用6个别名、批量提交、ByTid、下载队列；跨文档暂存撤销；
   尝试跨来源导航后未发生播放器提交。页面报告 BRIDGE_OK / SECOND_OK。
   WebView2跨来源页面明确报告native接口不可获得；IE跨来源页面未报告成功调用。
   结果：../out/runtime-20261022-final/{webview2,ie}/results.txt、page-events.json、browser.bmp。

4. 真实 PlayerWindow 隔离测试：DLL缺失、正常、缺导出的不兼容DLL，三种全部通过。
   检查主菜单、皮肤绘制/命中、tooltip、F11/global注册、播放列表下载菜单、快捷键行、
   网络下载设置页、设置round-trip。正常DLL检查提交曲目/Tid和读信息后标识保留。
   结果：../out/host-20261022-final/{absent,present,incompatible}/results.txt。
   视觉检查：present/browser.bmp（480x486）与browser-resized.bmp（600x566），
   原皮肤按钮、透明边角、下载页、复选框和九宫格边框在两种尺寸正常。

5. 静态旧系统导入审计：通过。
   主程序：x86 PE32，subsystem5.01，22 DLL/744 imports。
   ttpbrowser：x86 PE32，subsystem5.01，12 DLL/238 imports。
   EXE不静态导入ttpbrowser/WebView2Loader。审计记录：
   ../../reverse/music-window-5790/host-imports-final.json
   ../../reverse/music-window-5790/ttpbrowser-imports-final.json
   这不代替XP/Win7虚拟机运行测试；本次未进行旧系统VM验证。

6. 原网站：2026-10-22 HTTP HEAD观察到302跳转太合PC客户端下载页。
   本次没有确认原在线搜索、榜单或推荐服务可用，也没有恢复这些服务器。

最终部署到 D:\Projects\Backup\TTPlayer\：
TTPlayerRebuild.exe  4,335,104 bytes
  SHA256 595A9E85A6A66A0A418E134948CDA8723FB17AAE8C7FCEF8423BB5F697EC826C
ttpbrowser.dll        377,856 bytes
  SHA256 3C842661D2017F226E9F559B9B71FED0B3B64E7906FCBB8CC5421FEF0E0BCB7B
WebView2Loader.dll    116,304 bytes
  SHA256 1064AB1C2CDC0746B100F021514D591043DA7CC1583B450DD071D291080C53EF

原部署文件备份：../out/deployment-backup-20261022-034035/。
清单：../out/deployment.json。
未改写两个原版安装目录，未提交Git；原有rebuild/tests脏子模块保留。
