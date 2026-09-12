# I1-C Decoder 修复与验证记录

日期：2026-09-12。实际仓库：`/home/qq344/QLog`，WSL2 Ubuntu x86-64。

## 本轮结论

Decoder 已实现并加入 qlog 静态库，真实调用可编译、可链接。
修复 metadata 来源及错误映射、区域与数量校验、全部 tag 解析、Bool 校验、字符串借用、
错误定位、循环与零参数路径。保持 32B Header、32 槽 workspace、Release 永久边界检查。
失败不返回部分视图；完整参数才提交 workspace，不回滚此前槽位。

新增 `tests/record_decoder_test.cpp` 的 8 个测试组，以及声明头自包含翻译单元。
测试使用独立拼装 wire bytes，覆盖全部 15 个合法 tag、32 种输入偏移、Header 值副本、
字符串借用及空字符串、NullUtf8、0/32 参数、8192B format、Header 所有截断长度、
metadata 错误映射、区域错误优先级、全部未知 tag、全部非法 Bool 值、每类定宽值的所有
不足宽度、字符串长度前缀截断、字符串内容截断、错误 offset/index、部分提交与往返验证。
这不是完整 property/fuzz 或性能验收。

## 编译与测试

GCC 13.3 / Clang 18.1；C++20，项目警告选项及 `-Werror`。
六组均重新 configure/build 后运行全量 CTest，共 96 项。

| 配置 | 通过 | 预期跳过 | 失败 |
|---|---:|---:|---:|
| GCC Debug | 96 | 0 | 0 |
| GCC Release | 95 | 1 | 0 |
| Clang Debug | 96 | 0 | 0 |
| Clang Release | 95 | 1 | 0 |
| Clang ASan+UBSan | 96 | 0 | 0 |
| GCC software-only Release | 90 | 6 | 0 |

Release 跳过既有 Debug-only Ring corruption 测试；禁用硬件另跳过 5 项 forced hardware hash 测试。
新增 8 项 decoder 测试在所有配置均实际通过。ASan/UBSan 无报告。
标签核查：record_core=46、codec=17、format_hash=16、arguments=13。
ISA 选项仍只作用于硬件 hash TU。修改的 C++ 文件 clang-format 检查和 git diff --check 通过。

## 复现与证据

本轮复用 `build/test/i1-20260910-*` 六个配置；构建目录名称不代表本轮验收日期。
本轮命令、日志、JUnit XML、results.json 与改动文件 SHA-256 均在
`build/validation/i1-decoder-20260912/`。旧 20260910 验收日志保留。

```bash
python3 build/validation/i1-decoder-20260912/run_validation.py
```

## 后续边界

I1-C 已有 encoder/decoder 行为测试基线，但整个 I1 尚未完成。
下一步是 I1-D：完整 compile-fail driver、独立 property、长时 fuzz/corpus replay、coverage，
以及 Record Core benchmark/codegen 验收。不要将本轮确定性边界测试称为长时 fuzz。
