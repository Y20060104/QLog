# 原 HashMap 既有调用迁移决定

日期：2026-10-13。本文件只记录决定和验证边界；MISO 新实现仍由用户填写。

用户授权助手与其并发工作：“我写 miso，你调整其他文件”。本轮重新核对权威 QLog、最新 AGENTS、Git 与 BQLog 78cbfbef 提交源码，确认下列四处均应使用普通 hash_map；MISO TLS 缓存按参考使用 hash_map_inline。双版本原实现保留，不用 std::unordered_map 适配。

| QLog 成员/别名 | BQLog 对应源码 | 本次实际类型 |
| --- | --- | --- |
| Layout::thread_names_cache_ | src/bq_log/log/layout.h/.cpp | utility::hash_map<uint64_t, std::string> |
| Property::hash_type | src/bq_common/utils/property.h/.cpp | utility::hash_map<std::string, std::string> |
| PropertyValue::object_type | src/bq_common/utils/property_ex.h/.cpp | utility::hash_map<std::string, std::unique_ptr<PropertyValue>> |
| common_global_vars::file_exclusive_cache_ | src/bq_common/global/common_vars.h、两 native misc | utility::hash_map<file_node_info, file_open_mode_enum> |

Layout 构造恢复原 thread_names_cache_.set_expand_rate(4)。缓存文本采用当前 QLog 前缀，仍按首次线程 ID 名称缓存；tidy_memory 不清该缓存。其他三处扩容率保留原默认，不统一改成 4。

BQ 的 generic_type_hash_calculator 依赖键的 hash_code()，std::string 没有该成员。QLog 在现有 utility/type_tools.hpp 直接特化 generic_type_hash_calculator<std::string>，保持 std::string 模块合同。算法逐字符完全取自 include/bq_common/types/string_impl.h 的 get_hash：uint64_t 初值零，先乘 1099511628211ull，再异或 static_cast<uint64_t>(str[i])；按长度遍历，保留当前平台 char 正负语义和内嵌 NUL。不用 std::hash 或日志格式串 CRC 算法。文件身份直接复用已有 file_node_info::hash_code()。

同步原容器接口 add、is_empty、key()/value()。Property 的 parse 结果仍为 std::pair/std::vector，key_list 仍保留首次出现顺序，重复键覆盖原值；遍历 maps() 改用 item.key()/item.value()。PropertyValue 保留 reference 相同的 unique_ptr 子节点所有权和手动深复制，新对象字典按原默认构造并逐项 add；不引入 reserve/try_emplace 新包装。

公开 Property::hash_type 与 PropertyValue::object_type 已更换，调用方需要按原容器 API 编译；object_type::size_type 为原 uint32_t。HashMap 节点扩容会移动键和值，不能沿用 unordered_map 的元素地址稳定预期；PropertyValue 对外子节点引用仍由 unique_ptr 指向的独立对象保持稳定，已定向验证。哈希分布/遍历顺序采用原容器规则，不额外排序。

文件独占登记的锁、原 native 流程及删除规则按参考保留，不加引用计数、默认参数或防御断言。Property 的 EOF 未完成键值项仍跳过，保留其他有效项；int64_t 越界两个方案仍仅记录、未选定。

用户 MISO 头/cpp 和 platform/macros.hpp 不被助手写入。实际受改模块四配置严格编译通过；隔离副本只冻结上述三个依赖，四配置常驻 19/19、外部参考 4/4，四组差分输出一致。本轮不接受当前 MISO 并发/退出/恢复或性能。详见[验证记录](../validation/HASH_MAP_CALLERS_BEHAVIOR_20261013_CHS.md)。
