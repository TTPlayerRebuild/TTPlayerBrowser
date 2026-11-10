# 独立构建公共源码快照

`rebuild/` 保存从相邻 TTPlayer 重建项目复制的公共源码与声明，包括缓存索引、文本、ZIP 皮肤读取、TTPCOMM SDK 头及 Common-Controls v6 测试 manifest。

同步快照时，先将文本文件的换行统一为 LF，再计算 SHA-256 并更新清单。仓库的 `.gitattributes` 确保 Windows 和其他平台检出相同的快照字节。独立 CMake 配置会按清单逐个校验文件。[快照清单](rebuild/SNAPSHOT.json)

独立构建用此快照，嵌入主项目的构建仍使用相邻 `rebuild` 的实现。TTPCOMM 调用通过已有头文件与动态解析保持 ABI；不额外打包或静态导入 `ttpcomm.dll`。

原 SDK 声明见 [TTPCOMM-SDK-NOTICE.md](../docs/licenses/TTPCOMM-SDK-NOTICE.md)。
