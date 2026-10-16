# Que（阙）开发分支（2026-10-16）

分支为 `feat/que`，起点为完整版本 `c1c5e6cfc0466f788dad62521543a0883a01b6bb`。用户明确选择保留其他输出，仅移除压缩与加密实现。

开发代号采用 **Que（阙）**，延续 QLog 的 Q 标识。分支于同日由 `feat/no-compress` 重命名为 `feat/que`，功能范围保持本记录的既定选择。

## 范围

- 保留 Console、TextFile、Snapshot，三种线程模式，以及 Record/Layout/Buffer、配置与普通文件基础。
- 保留 `appender_type` 全部枚举值、顺序和配置名称，包括 `compressed_file = 3`；不改名为另一个枚举，不重新编号。Record 的预留 tag、format_hash 字段与通用哈希/UTF 转换也不改变。
- 移除压缩文件 Appender、仅由它使用的二进制文件容器基类、RSA/AES/Vernam，以及只由这条链使用的随机数、UTF-Mixed、VLQ/ZigZag 和有界模板缓存。
- 移除对应压缩测试、公开测试密钥、压缩 benchmark 场景与专用模板池。保留两项原普通 Text 场景和写侧延迟；保留 copy/hash、SISO、离线 Layout 诊断。

## 配置行为

配置 `compressed_file` 时，`log_imp::add_appender` 输出不支持诊断并返回 false，不构造替身，不退回 Text 或 Console。`init` 与 `reset_config` 仍沿用原来的 Appender 部分成功规则：跳过添加失败的 Appender，继续保留其他有效输出，不能据此将整个 logger 的返回值解释为全部 Appender 成功。

Snapshot 的实现与已有数据继续保留；生产者准入仍由有效 Appender 的等级合并决定，Snapshot 不单独开启生产者入口。只有不支持的 Appender 时，logger 对象仍按原流程创建，但日志等级合并结果不会因保留枚举或配置 Snapshot 而启用输出。

## 验证与历史数据

本分支注册 36 项回归，覆盖保留枚举与名称、无降级、有效兄弟输出、Snapshot 数据保留与原准入规则，以及 reset 恢复普通输出。GNU、原生 MSVC Debug/Release 各 36/36，构建与工具检查见[本分支验证](../validation/NO_COMPRESS_BRANCH_20261016_CHS.md)，不沿用完整版本的 44 项通过状态。

完整版本的压缩/加密决定和性能原始数据保留为历史材料，README 明确其适用源码。当前 benchmark 运行器核对 QLog 2 项吞吐、BQLog 19 项吞吐，只汇总双方共有的普通 Text 场景；不将未支持的压缩路径计入性能结果。

本轮在本地创建分支并保留工作区改动，未创建提交或推送。起点的 `AGENTS.md` 重写与 `src/buffer/log_buffer.cpp` CR 已备份；后者不作改动。
