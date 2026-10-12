# Buffer 配置审查与实际 CMake 收口

日期：2026-10-07。审查起点 QLog HEAD 1614ea56ff2d525313e3bd0399b96e6ff252e721，分支 feat/spsc-ring-opt；审查开始只有四个新增 Buffer 配置头/cpp 未跟踪，用户已提交其他此前草稿。BQLog HEAD 78cbfbef4f87e7558335f0c04bf6dd6894b46451，整树仍有 MISO/构建修改。配置映射/校验、平台分配与 NormalBuffer 采用文件无 HEAD 差异，MISO 本地差异另列于下一组指导。

最终落盘复核时QLog HEAD为f24f3afcd8e9418ea48ad5a6b05680c622008145，用户已在审查期间提交源码/构建修改；助手没有执行Git提交。提交前后已验证源文件SHA-256相同，无需把只改变Git身份当作代码行为变化。

## 发现、修正与更正

- include/qlog/config/buffer_config.hpp 声明 get_log_config_by_config，src/config/buffer_config.cpp 定义 get_log_buffer_by_config。两个 cpp 单独严格语法通过，但真实公开头文件调用方链接 undefined reference。已将已有声明/定义统一为先前指导的 get_log_buffer_config_by_config，参数/函数体/CR保持。
- utility helper 已由用户搬到 string_utils，但 runtime cpp 未包含其头，七处仍使用旧局部 equals_ignore_case 名称。实际 Debug/Release 编译失败。已包含 utility 头并将七处调用限定为 qlog::utility::equals_ignore_case，比较/等级规则与CR保持。
- 更正本轮早期 commentary：clear 已在最新头文件类内定义，不缺实现，也不是本轮链接缺陷。核对头文件与临时仅修helper调用后的 clear 调用方链接通过。按普通成员头声明/cpp定义约定，将现有 bitmap_=0 函数体迁到 cpp，无新增行为。不要重复在保留头文件函数体时再写 cpp 定义（临时误判探针曾重复定义，未写入生产）。
- 上一轮 categories_name/category_name 与 maks/mask 两处已由用户修正。
- LogBufferConfig 默认值、字段顺序、拼接文本/哈希，Buffer字段转换的整数类型、recovery&&平台能力、策略完整忽略大小写/不trim、阈值0转UINT64_MAX均符合已核对参考。校验仍保留初值0、先乘常数再xor、plain char转换语义与分隔歧义。

助手的生产修正严格限制为 include/config buffer头、src/config buffer cpp、runtime位图头/cpp与根CMake五文件；修正时其他生产SHA-256保持，CR逐项比较保持。随后观察到七个文件再次保存为当前格式（含defs/config/utility头与相关cpp），已比对并保留，不将这些格式变化算作助手额外实现。以最终保存版本重新完成Debug/Release构建、CTest各12/12与实际配置48/过滤61个UBSan检查；最终验证开始/结束135个文件SHA-256一致，清单为final-validated-manifest.json。新平台模块只在临时目录/文档准备，没有创建生产声明/定义，tests未修改。

CMake新增六个已填写cpp：
src/buffer/log_buffer_config.cpp、
src/config/property.cpp、src/config/property_value.cpp、
src/config/filter_config.cpp、src/config/buffer_config.cpp、
src/runtime/log_level_bitmap.cpp。
保持PUBLIC QLOG_DEBUG仅Debug定义，不定义为0，不在调用方改该宏。argument_serialization等模板头没有空cpp。

## CR回答

1099511628211 == 0x100000001b3 是64位FNV乘数，常数来源可核对 [RFC 9923 第5节](https://datatracker.ietf.org/doc/html/rfc9923#section-5)。BQLog实际使用初值0、乘后xor，与标准FNV-1a不同；源码未给出作者选型实验，不能声称它对所有日志名称最优。从算法看，乘法与xor让前一状态和当前字符共同参与下一轮；奇数与2^64互素，单独乘法不会像偶数乘数那样折叠全部状态，这只是一个数学属性，不足以证明整体散列质量。这里更换常数会更改恢复身份值；参考MISO checksum不符时放弃恢复、重新初始化，因此不能当作任意性能调参项。字符符号、初值、顺序、拼接文本均与兼容结果有关。

PropertyValue表示一个带类型的递归配置节点，当前tag包含bool/double/int64/string/array/object/null等；object的key指向子PropertyValue，array保存子节点。log_config是config["log"]对象节点，可承载buffer_size等不同类型字段；LogBufferConfig是转换后供Buffer使用的明确字段集。不是用一个enum解释全部配置。

buffer_policy_when_full是约定的配置键，不是C++枚举项。节点字符串discard/block/expand分别映射discard_when_full/block_when_full/auto_expand_when_full；缺键、错误类型或未知字符串保持默认block。键名不会随枚举名字自动改变，配置树也不会自动补齐它；const查不到键返回null节点，is_string为false。整数越界两个方案继续仅记录、暂不选定，当前double分支仍只是草稿。

## 实际验证与限制

目录：/tmp/qlog-buffer-config-review-20261007-7o65b58i。actual-consumer是仅通过QLog::qlog链接公开头文件调用方的外部CMake工程，没有直接将配置cpp编译到调用方或引入临时替代生产头。

- 实际根qlog目标Debug/Release严格构建、调用方链接通过，CMAKE_CXX_FLAGS=-Werror，根目标/消费者含-Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow。
- 每种配置CTest各12/12：项目已有1个smoke与9组Layout，加外部工程中的2个临时配置调用方。项目tests/CMakeLists未新增这两个测试，因此项目常驻CTest仍10项，不能称已建立永久配置回归。
- 调用方Buffer组48个定向检查、过滤组61个；Buffer每轮12组checksum直接调用当前参考实际头文件log_buffer_config::calculate_check_sum，非仅复制公式。覆盖默认/类型/转换/平台能力/策略/阈值/实际构树、字符/顺序/分隔边界；Appender部分只验证摘录条件，不调用真实Appender。
- 实际配置/过滤源码同组48与61个定向UBSan + float-cast-overflow检查通过；这不是整个库/所有输入的sanitizer验收。
- compile_commands核对两种配置的Ring、全部配置/bitmap cpp及两个消费者：Debug定义QLOG_DEBUG且未定义NDEBUG，Release未定义QLOG_DEBUG且定义NDEBUG；消费者只链接目标继承，不独立切换宏。
- 这证明当前配置/过滤/Buffer配置可编译、链接、CMake接入与所测行为，不证明整数越界合同选定、完整配置差分、完整异步日志链、MISO并发/恢复、Windows/移动端或性能通过。
- 助手未执行Git提交，无额外生产Manager/Worker/Appender/平台实现。

用户已选择“先按实际依赖补平台存储基础，再实现MISO”。下一组为platform/aligned_memory完整函数，后续文件/MemoryMap→NormalBuffer→MISO。详见 [平台对齐内存完整指导](../decisions/PLATFORM_ALIGNED_MEMORY_IMPLEMENTATION_GUIDE_20261007_CHS.md)。

## 2026-10-07 本次记录同步复核

本次只更新AGENTS、总路线、R0当前入口、配置/过滤/Buffer配置及平台指导的进度说明和相关历史更正，没有修改include/src/tests/root CMake，也没有执行Git提交或重新运行编译/测试。

- 实际QLog HEAD为f24f3afcd8e9418ea48ad5a6b05680c622008145；根qlog target列出13个存在的cpp，常驻CTest仍为1个smoke+9组Layout，共10项。
- 此前验收覆盖的135文件与本目录final-validated-manifest.json全部SHA-256一致；复核期间新增两个平台草稿，因此当前保护清单为137文件，这两个草稿未纳入已有验收。已重新读取final-Debug-ctest.log、final-Release-ctest.log及定向UBSan运行日志，确认此前各12/12、Buffer48（实际BQ checksum12）/过滤61的记录；这是已有证据复核，不是本次重跑。
- 复核期间新增include/qlog/platform/aligned_memory.hpp与src/platform/aligned_memory.cpp声明/函数体草稿；静态快照中释放名align_free/aligned_free与命名空间platform/platfrom尚不一致，留待用户完成后实际审查。平台分配未接CMake/验证，不能将已有候选结果计作这些新草稿通过。文件操作/MemoryMap→NormalBuffer→MISO是用户已确认的依赖路线，不能把临时候选记作生产平台实现。
- 参考HEAD仍为78cbfbef4f87e7558335f0c04bf6dd6894b46451。已核对配置/过滤/哈希、平台分配、platform/io下MemoryMap、NormalBuffer与TimeZone相关采用文件，无HEAD差异；MISO/构建本地修改仍在，不能称整树干净。
- 整数越界两备选保持“仅记录、暂不选定”；当前double草稿不是默认政策。本次不扩大验证结论，Ring/MISO并发与恢复、完整配置差分、DST专项、Windows/移动端及性能仍未验收。

早期FILTER_CODE_REVIEW中的clear缺定义/链接失败结论已增加更正说明；最新实际问题与修正以上文为准。文档同步后的链接检查与git diff --check通过，包含两份新草稿的137文件在此次落盘前后校验值保持，CR随生产源码保持。
