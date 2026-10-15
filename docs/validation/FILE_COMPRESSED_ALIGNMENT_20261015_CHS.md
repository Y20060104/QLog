# FileCompressed 实际实现与验证（2026-10-15）

> **后续状态：** 以下为初次实现及当时源码的验证快照。用户此后明确接受RSA原零整数越界复制，两处安全返回及两条安全断言已撤回；不能把下文“零边界”历史检查当作当前安全保证。当前四配置回归与复测见[本轮记录](BQLOG_NAMING_RSA_BENCHMARK_20261015_CHS.md)，原40文件/24008记录互通数字未在本轮重跑。

压缩文件模块及必要依赖已写入 `/home/qq344/QLog` 并接实际 CMake。参考固定提交78cbfbef4f87e7558335f0c04bf6dd6894b46451，采用源码16项与 HEAD/固定导出逐项一致，参考整树仍脏且未修改。决定与差异见[对齐决定](../decisions/FILE_COMPRESSED_ALIGNMENT_20261015_CHS.md)。

## 实际变更

新增 `appender_file_binary`、`appender_file_compressed`，原bounded缓存、VLQ/ZigZag、UTF-Mixed、随机数、对齐分配适配，以及原RSA/AES-CBC/Vernam依赖；`log_imp::add_appender` 接 `compressed_file`。本轮已有生产文件只改根CMake及工厂cpp，另改测试CMake；原平台、Buffer、Record、Layout生产源码不变。开始时247个文件已保存字节副本及SHA-256，用户已有未提交实现和CR保留。

## 构建与常驻回归

| 环境 | 库cpp | 全部TU | Debug | Release |
| --- | ---: | ---: | --- | --- |
| GNU/Linux | 57 | 67 | 严格构建、40/40实际CTest | 严格构建、40/40实际CTest |
| 原生MSVC x64 | 58 | 68 | 严格构建、40/40实际CTest | 严格构建、40/40实际CTest |

GNU使用根警告及-Werror。MSVC使用/W4 /WX /permissive-，仅保留外部验证原有/wd4324 /wd4146，未新增warning豁免。逐编译命令核对：QLOG_DEBUG只在Debug定义且库/调用方一致；NDEBUG只在Release，未改根PUBLIC规则。

新增9组 `qlog.compressed.*`：

- primitives：VLQ所有1–9字节阈值、独立解码比对、ZigZag有符号边界、UTF-Mixed内容/原UTF16哈希、bounded缓存硬上限、AES-256-CBC已知向量、原公开2048位RSA样例及零边界、XOR相对对齐与key环绕。
- scalars：指针/null/bool/字符、各宽整数边界、浮点原始位、UTF8/UTF16/普通UTF32，验证压缩参数tag和实际值。
- sync、async、independent：实际公开入口、MISO运输、独立Worker、格式/线程模板引用、UTF-Mixed及长度占位、正文/数量/逐生产者顺序；independent为4生产者，其余1生产者。
- cache_reset：实际配置缩小及300种额外格式，验证模板引用仍有效。
- reuse：关闭旧Appender后真实复用同一个明文文件，重建模板哈希并续写UTF16格式。
- rollover：4096字节限制下产生多个完整压缩文件，核对全部序号无缺失重复。
- encrypted：原公钥配置、分段RSA/AES-CBC/XOR加解密及600条正文。

自包含测试的读取器按文件格式独立解析VLQ和模板引用；加密先解session key/blob，再逐字节XOR核对，不用Vernam写路径代替读取。完整生产Decoder没有加入QLog。

## 固定BQLog原解码器互通

使用上一轮 `git archive` 固定导出的BQLog官方Release库，外部小调用方只调用原 `bq::tools::log_decoder`；没有修改解码器来兼容QLog。每个配置8组文件共6002条，其中包含加密、文件复用、滚动及整数边界。

| QLog文件生成配置 | 文件数 | 原解码器读出记录 | 正文/数量/重复/顺序 |
| --- | ---: | ---: | --- |
| GNU/Linux Debug | 10 | 6002 | 通过 |
| GNU/Linux Release | 10 | 6002 | 通过 |
| 原生Windows Debug | 10 | 6002 | 通过 |
| 原生Windows Release | 10 | 6002 | 通过 |
| 合计 | 40 | 24008 | 通过 |

滚动文件逐文件解码并做全体序号集合核对；其余多记录组额外按生产者核对顺序。scalar逐项核对原解码器正文及自包含读取器中的值/浮点位；不声称这覆盖所有格式、损坏状态或所有硬件。

## 可复现入口与证据

```properties
log.thread_mode=async
log.buffer_policy_when_full=block
appenders_config.file.type=compressed_file
appenders_config.file.levels=[all]
appenders_config.file.file_name=logs/application
appenders_config.file.format_template_cache_max_entries=100000
appenders_config.file.thread_info_cache_max_entries=2048
```

输出扩展名为 `.logcompr`。需要参考的加密链时添加 `appenders_config.file.pub_key=ssh-rsa ...`，实际互通使用2048位密钥；测试样例密钥是BQLog公开测试资料，不能用作实际私钥。

```sh
cmake -S /home/qq344/QLog -B /tmp/qlog-compressed-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build /tmp/qlog-compressed-debug -j 6
ctest --test-dir /tmp/qlog-compressed-debug --output-on-failure
```

证据根：`D:/qq344/Documents/qlog-file-compressed-alignment-20261015-01`，WSL入口为`/mnt/d/qq344/Documents/qlog-file-compressed-alignment-20261015-01`。

- `before/`、`before-hashes.json`：原247项保护快照；`reference-sha256.json`：16个BQ采用文件。
- `validation-matrix.json`：四配置cpp/TU、宏传播和40/40结果。
- `linux-{Debug,Release}-complete-{build,ctest}.log`、`native-{Debug,Release}-{build,ctest}.log`：最终实际日志。
- `bqlog-cross-decode-matrix.json`：四配置每文件输入/输出哈希、条数；`oracle-matrix-decoded/`保留原解码器文本及调用日志；`cross-decode-matrix.py`可重跑现有产物互通。
- `qlog-final.json`、`bqlog-final.json`、`source-protection-audit.json`和`final-source-sha256.json`：最终状态、变更边界及源码清单。

初次迁移限定名/缺头、标准容器追加转写以及Release断言变量警告均已修正，失败日志保留。零明文测试原先误期望encrypt成功，已按原拒绝规则改成独立零密文decrypt/零key解析检查。最初外部runner依赖/tmp文件却未检查目录存在，零条结果不是解码器实际EOF；改为构建目录持久产物并显式检查输入。Windows CTest日志本地编码只按ASCII artifact路径提取，实际解码正文仍严格UTF8检查。最终四配置和原解码器结果如上。

## 验证边界

这是实际压缩模块回归及固定参考文件互通；不是新的Compress吞吐、压缩率或延迟压测。上一轮Text/Console性能数字保持历史范围，不推广为本模块结果。未运行TSAN、ARM、长期压力、完整异常/断电/短写故障注入，未把正常文件复用称为崩溃恢复验收。force_flush到OS缓存的边界不变。所有未决配置整数越界/custom UTF32及已接受参考边界继续保留。
