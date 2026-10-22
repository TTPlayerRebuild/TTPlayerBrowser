# 独立构建公共源码快照

`rebuild/` 保存从相邻 TTPlayer 重建项目复制的 11 个必要文件：缓存索引、文本、ZIP 皮肤读取、相关声明、四个 TTPCOMM SDK 头及 Common-Controls v6 测试 manifest。它不包含播放器主程序、网络引擎、音频解码器或私有测试数据。

来源和字节哈希记录在 [SNAPSHOT.json](rebuild/SNAPSHOT.json)。这些文件在同步时逐字节复制；独立 CMake 配置会验证清单。主项目中的公共实现有变化时，应明确同步所需文件并更新清单，不应以运行时检出浮动分支替代快照。

独立构建用此快照，嵌入主项目的构建仍使用相邻 `rebuild` 的实现。TTPCOMM 调用通过已有头文件与动态解析保持 ABI；不额外打包或静态导入 `ttpcomm.dll`。

原 SDK 声明见 [TTPCOMM-SDK-NOTICE.md](../docs/licenses/TTPCOMM-SDK-NOTICE.md)。
