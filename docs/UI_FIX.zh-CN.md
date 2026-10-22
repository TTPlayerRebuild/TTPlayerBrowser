# 音乐窗外围绘制与下载管理修复 / 2026-10-22

## 原版依据

两个原版安装目录的TTPlayer.exe及ttpres.dll内容一致。本次以原版z文件为目标，
复用并补充REA/Ghidra静态证据，重新提取`<DOWNLOAD_SKIN>`的XML及位图。
未运行或改写原版安装目录。具体地址、Evidence ID和未确认项见：
../../reverse/music-window-5790/download-native/UI_EVIDENCE.zh-CN.txt
../../reverse/music-window-5790/download-native/FINDINGS.zh-CN.txt
../../reverse/music-window-5790/resources/download-skin/manifest.json

## 外围窗框

原版创建Browser使用WS_POPUP/WS_EX_TOOLWINDOW；重建版多加WS_THICKFRAME，
仅处理WM_NCCALCSIZE不足以阻止系统/DWM绘制，导致皮肤周围出现额外框线。
现已移除系统缩放框，由客户区实现拖动、八方向缩放、Esc撤销和捕获释放。
窗口区域与皮肤使用相同的九宫格/平铺算法，透明角及内容区随尺寸、换肤更新。
DWM边框、圆角策略从系统目录动态获取，不增加旧系统不支持的静态DWM依赖。
此前Common-Controls v6提示文字查询引发闪退的修复保持不变。

## 下载页

按原资源恢复顶部横幅、使用技巧链接、下载设置按钮，左分类80px、分隔5px、
表头25px、行高24px，以及Arial/SimSun字体、交替行色、状态位图和进度条。
下载中与已下载使用不同表头；列宽依原函数00420E99/004232E6比例随可用宽调整。
移除此前额外的常驻底部按钮和说明区，主要操作回归原版右键菜单。
设置入口交给主程序打开网络/下载选项，帮助链接使用原版完整URL。
暂停会停止任务并持久保存paused状态，重新打开播放器后不会自动恢复该任务。
多选播放/添加一次性提交给宿主，已下载项目使用本地文件路径。

## 实现与复验边界

这是依据原版资源和原生代码恢复的实现，不是原版源码或逐像素等同认证。
横幅及滚动条部分拉伸细节未取得完整原版执行证据；未在XP/Win7虚拟机验收。
日期用已下载文件最后写入时间显示月-日；历史服务提供的独立下载日期未恢复。
发送到支持文件夹；原版动态发送目标扩展未重建。原音乐网站后台不在本次修复范围。
暂停后的开始重新请求来源，是否复用已缓存字节由宿主传输层决定，未宣称恢复
原网站协议的断点续传。旧列表导入/清理逻辑仍保留内部调用，不插入原版右键菜单。

## 验证、截图及部署

完整记录保存在 ../out/ui-fix-20261022/，最终清单为 verification.json。
契约61项断言和下载模块两套CTest全部通过；八方向缩放、透明角、Esc撤销、
暂停持久化、多选一次提交本地文件、设置入口均包含在回归中。
host-final2/使用实际重建资源验证DLL正常/缺失/不兼容，三种均通过；附默认和放大截图。
runtime-final2/记录真实WebView2及IE各10项通过；只访问本机测试页面。
downloads-ui/保存有任务的原尺寸与放大截图，含真实流读取形成的50%进度。
release-startup.json记录指定Release EXE正常显示主窗口和音乐窗，8秒后正常关闭，
退出码0、捕获异常0；用户原XML及备份文件在测试后恢复。最终静态导入审计也通过。
部署前文件备份在 ../out/ui-fix-20261022/deployment-before/。
主程序实际集成回归可重跑：
  python ttpbrowser/tests/run_host_tests.py rebuild/build --output <新建输出目录>
使用复制的重建资源和运行库，分别验证DLL正常、缺失、不兼容；不会复用用户配置。
