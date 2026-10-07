# 配置文本 Property 对齐决定与当前进度

日期：2026-10-05；最近源码复核：2026-10-07。
QLog 核对 HEAD：ec3f72b27784b3b1a9047391117395b4848afb8d，工作树有未提交修改和未跟踪草稿。
BQLog 参考 HEAD：78cbfbef4f87e7558335f0c04bf6dd6894b46451；本次 property.cpp 与 HEAD 无差异，不能据此称整个参考树干净。

## 2026-10-07 后续过滤兼容方向确认（最新）

用户确认 Logger/Snapshot 与 Appender 类别过滤按 BQLog 各自规则保留；具体匹配、调用顺序、fallback 与模块目标见 [过滤完整指南](CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md)。本次重新读取 actual源码，PropertyValue 前轮三处编译修正保持；LogLevelBitmap/filter_config 四个目标文件尚未创建，不根据历史指南称它们已实现。

本轮只更新决定和完整指导。位图/过滤候选连接实际 PropertyValue/Property/string_utils，Debug/Release严格编译链接及定向UBSan通过；另验证9组类别规则差异和全局拒绝不能绕过的组合。Appender只使用参考匹配条件摘录，不是正式实现/原库差分。证据见 [FILTER_RULES_GUIDE_20261007_CHS.md](../validation/FILTER_RULES_GUIDE_20261007_CHS.md)。所有 include/src/tests/CMake 文件 SHA-256 保持不变；配置 cpp 尚未加入根 qlog target，整数越界两方案仍仅记录、暂不选定。

## 2026-10-07 实际构树编译修正（已完成快照）

用户明确要求助手落实上一轮三处修正并确保实际源码编译通过。最新源码重新核对后，仅修改 src/config/property_value.cpp：

1. 调整 cpp 私有函数定义顺序：识别/转换 helper 在前，parse_to_property_value、trans_parson_to_property_value 在后，仍在匿名命名空间内。C++ 调用位置必须先看见函数声明；不将这些内部 helper 暴露到头文件。
2. parse_null_value(value.c_str()) 删除误传的 bvalue 参数，与实际单参数定义及 BQLog property_ex.cpp:139 一致。
3. PropertyValue::create_from_string 整块移到匿名命名空间关闭之后，仍在 qlog::config 中。类成员定义必须在类所属的外围命名空间中；匿名子命名空间不是其合法定义位置。工厂仍可通过匿名命名空间的文件内可见性调用 trans helper。

通过非空代码行多重集合比较确认，除 null 多传参数删除外，代码行及所有 CR 均保留；清理一行搬移后的空行尾空白；未整体格式化、未修改数值/数组/路径规则。include/src/tests/CMake 全量文件快照核对，唯一变更是该 cpp。BQLog 参考 HEAD 仍为 78cbfbef4f87e7558335f0c04bf6dd6894b46451，property_ex.cpp 无 HEAD 差异。

实际两个配置 cpp 在 Debug/Release 严格语法检查通过。公开头文件调用方分别编译实际 property.cpp/property_value.cpp 并链接实际 qlog 库，两种配置均严格构建通过，构树定向 CTest 各1/1通过，PUBLIC QLOG_DEBUG/NDEBUG 状态核对通过。专门 float-cast-overflow 检查直接编译实际源码并运行，同样通过。本次不再以临时生产副本替代实际配置 cpp。

证据见 [CONFIG_TREE_COMPILE_20261007_CHS.md](../validation/CONFIG_TREE_COMPILE_20261007_CHS.md)，目录 /tmp/qlog-config-tree-fix-dtwqknoc 保留修改前源码、patch、哈希、公开调用方、构建脚本及日志。根 qlog target 仍未列配置 cpp，本轮没有改 CMake；临时调用方构建不等同正式配置集成或 BQLog双库差分/并发/Windows/性能验收。

现有数值草稿分支保留 double，用户的两个越界方案继续仅记录、暂不选定，不由本轮编译修正作出政策选择。后续仍按过滤完整指南由用户填写 LogLevelBitmap 和两个 config helper，实际审查后收口配置/过滤的 CMake 与正式测试。

## 2026-10-07 用户构树实现审查（修正前快照）

实际文件新增了三个构树函数及头文件 public static 工厂声明，主流程与 BQLog property_ex.cpp:106-180 一致：数组分支直接修改 root、返回空 array 标记防止上层覆盖、原始 key 与点号尾部保留、通过 Property::maps 构树而非原始键值序列。

当前生产 Debug/Release 严格语法检查均失败，三个阻塞仍待用户修正：

1. property_value.cpp:100 的 parse_to_property_value 调用了后置定义的识别/转换 helper，却没有前置声明。把 parse_to 与 trans 两段整体放到 try_convert_integral_double 之后、匿名命名空间关闭之前；serialize_recursive 可以保留原位。
2. :139 的 parse_null_value(bvalue,value.c_str()) 与单参数定义不一致，应调用 parse_null_value(value.c_str())。
3. :174 的 PropertyValue::create_from_string 位于匿名命名空间内；将成员定义整体放到匿名命名空间关闭之后，仍处于 qlog::config 中，头文件 public static 声明无需移动。

助手本轮未修改生产头/cpp、CMake、现有测试和 CR。重复 cmath include 是整理项，不作为行为或编译阻塞。只在 /tmp/qlog-config-tree-audit-00gyel3t 中从最新生产快照作上述三个调整；临时副本 Debug/Release 严格编译、实际链接和构树定向行为通过，专门 float-cast-overflow 检查通过。覆盖层级/类型、空与混合数组、原空项和空白项、重复 key[]、末尾未完成项、异常点号/[]、NUL 入口、拥有型返回值、double-first 精度和边界。生产受保护文件 SHA-256 不变，不能将临时通过计作生产树通过。

最新 parse_to 的数值分支实际采用“不能转 integral 则存 double”。这只是当前草稿事实；用户明确要求两个整数越界方案仅记录、暂不选定，本轮不把代码事实替换为正式选择。原字符串备选仍保留；若后续收口需要选择，须由用户明确决定，不默认 double。过滤 helper 指导可以独立推进，不依赖此越界选择。

参考 HEAD 重新核对为 78cbfbef4f87e7558335f0c04bf6dd6894b46451，property.cpp/property_ex.cpp/property_ex.h、log_level_bitmap.h/.cpp、log_utils.h/.cpp、log_imp.cpp 与 appender_base.cpp 均无 HEAD 差异。参考整树仍有 MISO 与构建本地修改。

下一组完整指南见 [CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md](CONFIG_FILTER_IMPLEMENTATION_GUIDE_20261007_CHS.md)：等级位图属 runtime，配置过滤转换属 config。已区分 Logger/Snapshot 的点号边界类别规则和 Appender 的普通前缀/星号规则；不合并为统一 matcher。该指南候选在临时目录完成 Debug/Release 严格编译链接、定向运行与 UBSan，尚未建立任何生产模块文件。没有运行 BQLog 双库差分、并发、Windows 或性能测试，配置 cpp 尚未接 CMake。

后续顺序：先修正三个编译阻塞 → 写等级位图及两个过滤 helper → 复核实际源码与配置/过滤 CMake 和定向行为收口 → 继续 Buffer 运行态配置、MISO/TLS/LP-HP。Worker/Appender 正式实现后置；Appender 的类别规则当前只作为参考边界说明。

## 2026-10-06 整数 helper 收口与构树候选指导（历史快照）

本轮开始时 try_convert_integral_double 的强转曾写作 std::int64；用户在审查过程中已修正为 std::int64_t，以重新读取的最新源码为准。当前整数形判断、[-2^63, 2^63) 范围检查、失败保留 out 与既有指导一致。cpp 头部有重复 cmath include，属于可整理项，不影响本轮严格编译。fmod CR 保留，指导说明它计算浮点余数，除以 1 的余数为零表示没有小数部分；不是整数取余运算符 %。

最新生产 PropertyValue cpp 的 Debug/Release 严格语法检查通过。直接包含实际生产 cpp 的临时探针在 Debug/Release 与专门 float-cast-overflow 检查下均通过，覆盖六个识别 helper 及当前整数转换函数，包括 errno/-0、数值前缀、负号拒绝、合法边界、超范围、小数、非有限值、失败输出不变和 double-first 精度损失。不是正式 qlog 配置 CMake 接入或完整配置树验收。

QLog HEAD 仍为 ec3f72b27784b3b1a9047391117395b4848afb8d；BQLog 参考 HEAD 仍为 78cbfbef4f87e7558335f0c04bf6dd6894b46451。参考 property.cpp/property_ex.cpp/property_ex.h 与 HEAD 无差异，整树仍有 MISO/构建等本地修改。已重新核对 property_ex.cpp:106-180 的三个构树函数及 :418-428 的可变对象下标行为。

后续指导将 parse_to_property_value、trans_parson_to_property_value、public static PropertyValue::create_from_string 作为一组完整说明。前两个放 PropertyValue cpp 已有匿名命名空间，按依赖顺序位于整数转换 helper 后；工厂在头文件 public 声明，在 cpp 使用 PropertyValue:: 限定名定义。复用 qlog/utility/string_utils.hpp 的 trim/split_nonempty，另包含 qlog/config/property.hpp，不重复建立文本 helper。

整数形数值越界仍只记录两个备选，不选定任何默认。为了使后续指导具体可审查，本轮准备并分别验证了完整 double 候选和原字符串候选；两套都只是临时指导材料，不作为已接受实施规则，也没有写入生产声明/定义。两个候选的公共流程相同，只在数值分支不同：double 候选将不能转 integral 的数值保留 double；原字符串候选仍把正常小数存 double，仅对整数形且转换失败者保存传入 parse_to 的 value。这里的原字符串指 Property 规范化/反转义或数组项 trim 后传入的值，不承诺还原原始文件行。

数组识别调用入口也保持参考的 parse_array_key(key.c_str()) 与 parse_array_value(value.c_str())，由现有 string 按值参数接收，不能静默换成整段 std::string。候选新增内嵌 NUL 的键和值检查，确认 C 字符串识别边界与后续完整字符串 substr 的参考组合规则；两套候选重新通过 Debug/Release 与 float-cast-overflow 检查。

parse_to 顺序保持参考：数组键、数组值、数字、bool、null、字符串。数组实际内容直接写 root[key]，返回的空 array 只作为上层跳过覆盖的标记。数组键判定是包含 []，随后去掉 key 最后两字符；数组按逗号 split 丢原空项，再逐项 trim，不提供 JSON/嵌套语法保证。保留 levels[]=[info,error] 的空键数组副作用及返回空数组标记行为。

trans 保留原始 key 传入 parse_to；路径多段时用 key.substr(keys[0].size()+1) 保留原始尾部，不能把异常点号路径重新 join 成规范路径。实际候选检查确认 a..b=[x] 建在 a 的 .b 键、levels[]tail=x 按包含判断去尾两字符建在 levels[]ta。工厂遍历 Property::maps()，不改成 keys 或原始 parse 行；重复 levels[] 文本仅最后一项，空/全注释输入根保持 null，父子路径冲突仍受哈希遍历顺序影响。

本轮目录 /tmp/qlog-config-tree-review-mlu7eeag 保留最新生产快照、SHA-256、实际 helper 探针、两个完整候选与日志。两套候选各自通过 Debug/Release 严格编译、链接、定向树行为和专门 float-cast-overflow 检查；覆盖层级标量/数组、重复项/EOF、空根、输入字符串所有权、转义、参考数组及点号/数组键异常规则、2^63 边界和 double-first 舍入。两个候选均链接实际 Property 与共享 string_utils。不是生产 create_from_string、完整 sanitizer、BQLog 双库差分、Windows、运行态/Buffer 或性能验收。

本轮未改生产源文件、头文件或 CMake，配置 cpp 仍未加入 qlog。下一步由用户落实构树代码；整数越界规则继续待决，闭合该分支时需要选定两种备选之一。之后进行生产审查/链接/行为收口与配置 CMake 接入，再进入过滤 helper/运行态参数及 Buffer。

## 2026-10-06 字符串工具统一抽取（先前快照）

本节优先于下方同日快照。用户明确授权助手修改放置 trim 与 split_nonempty；两个函数现统一归属 qlog::utility，不再要求在 PropertyValue cpp 复制定义。

已新增 include/qlog/utility/string_utils.hpp 声明与 src/utility/string_utils.cpp 普通非 inline 定义；迁移的是原 Property cpp 的函数体，split_nonempty 中的 CR 随函数保留。TimeZone 私有 trim 声明/定义与 Property 匿名命名空间中的两个重复定义已移除，所有现有调用改为 qlog::utility 限定名。原 utility.hpp/round_pow_of_two、PropertyValue 和其他生产草稿没有修改。

BQLog 参考 HEAD 再次核对为 78cbfbef4f87e7558335f0c04bf6dd6894b46451。trim 声明位于 include/bq_common/types/string_def.h:180，定义位于 string_impl.h:401；split 定义位于 string_impl.h:377。TimeZone、property.cpp、property_ex.cpp 共用字符串成员；上述参考文件与 HEAD 无差异，整树仍有 MISO/构建等本地修改。QLog 使用 std::string，因此以独立 utility 自由函数承接同类字符串操作，不新增字符串包装类。

trim 保持 unsigned char 后调用 std::isspace 的规则，去掉两端空白、保留内部内容并返回副本。split_nonempty 保持单字符分隔和丢弃原空项，不自动 trim：a,, b, 得到 a 和带前导空格的 b。replace_all/find_split 继续留在 Property cpp；数值识别、try_convert_integral_double 与树转换继续属于 config cpp 的内部逻辑。

根 CMake 已加入 src/utility/string_utils.cpp。两个 config cpp 仍未加入 qlog 库；本轮 Property 是作为临时调用方的源文件编译并链接实际 QLog 库，不能记为配置模块正式构建接入或完整树验收。

验证目录：/tmp/qlog-string-utils-migration-us5u4b29。该目录保留修改前快照、保护文件 SHA-256、临时 CMake 调用方及 Debug/Release 日志。两种配置均以现有完整警告选项加 -Werror 构建通过；每种配置 CTest 12/12（仓库现有 10 项 + 临时 2 项）通过。临时调用方覆盖共享字符串 helper、TimeZone 的带空白 UTC/固定偏移/local 别名、实际 Property 解析/EOF/load 合并/serialize 回归。编译命令检查确认库和调用方继承相同的 Debug-only QLOG_DEBUG，NDEBUG 状态符合配置。

新 utility 两文件 clang-format --dry-run --Werror 通过，git diff --check 通过。静态核对确认迁移函数体逐字一致，原有普通注释与 CR 全部保留，其他生产草稿 SHA-256 未改变。该验证不是 BQLog 双库差分、Windows、local DST 行为、完整配置树/Buffer 或性能验收；没有新增生产测试文件或修改 tests/CMakeLists.txt。

最新源码另已包含用户补写的 errno = 0 与 -0 前导零检查，这两项不是本轮助手修改。PropertyValue 后续需要文本操作时，包含 qlog/utility/string_utils.hpp 并直接调用共享函数；不再新增局部 trim/split_nonempty。下一项仍由用户填写 try_convert_integral_double。整数越界后保留 double 或原字符串继续仅记录两个备选，暂不选定；树写入三函数在该规则明确后闭合。


2026-10-06 后续指导已聚焦完整 try_convert_integral_double 定义：放在 src/config/property_value.cpp 已有匿名命名空间，parse_array_value 后、命名空间结束前；不增加 public/private 类成员声明。使用参考 fmod(dvalue, 1.0) 的整数形判断，再检查 [-2^63, 2^63) 合法区间，最后才执行 static_cast<int64_t>。失败不修改 out，也不决定 decimal/string 回退。它是参考 property_ex.cpp:130-133 的内部抽取与已讨论的安全边界，不是 BQLog 现成的同名独立函数。

此次指导候选与最新生产 PropertyValue 的六个识别 helper 在 /tmp/qlog-integral-double-guide-6175w9ti 临时副本完成 Debug/Release 严格编译、链接和运行，以及专门 float-cast-overflow 检查。覆盖已补 errno/-0 规则、数字前缀、负号仍拒绝、普通/负下界/正上界/小数/巨大/非有限 double、失败输出不变、int64 最大值文本先舍入到 2^63、2^53 以上 double-first 精度损失。未写入生产函数，未重复运行现有 CTest，不计配置树、完整 sanitizer 或 BQLog 双库差分验收。

## 2026-10-06 接续审查与越界备选记录（先前快照）

上一轮四处问题已由用户修正：冒号回退、数组分支判断、对象行尾换行、整数序列化。六个值识别函数也已填写。

本轮发现并按此前参数同步授权修正两个已实现函数的引用拼写：`ouut -> out`、`sr -> str`。生产两 cpp 随后通过 Debug/Release 严格语法检查。CR 保留，新树转换定义没有写入生产文件。

当前仍需用户修正：parse_number_value 的 `errno;` 必须为 `errno = 0;`；is_decimal 少了参考针对 `-0` 的前导零检查。生产 helper 实际探针在预置 EINVAL 后拒绝正常 `12`，out 保持 77；独立 is_decimal(`-01`, 3) 返回 true。后者暂不改变当前拒绝负号的公开数值识别路径，但不是完整参考实现。

生产 Property 单独调用方的 Debug/Release 严格编译、链接和解析/load/serialize 定向运行检查均通过；配置树整体尚未生产验收，配置 cpp 未接 CMake。

用户本轮要求记录整数越界的两个方案。按此前上下文，原句的“int64_t 范围时”补全为“**超出 int64_t 范围时保留为 double；另一种选择是保留原字符串**”。范围内的整数形数值仍按参考存 integral_type，普通非整数存 decimal_type。

用户随后明确回复：“仅记录两个方案，暂不选定”。因此两个方案均只作为待决备选，当前不把 double 或原字符串设为默认实现。

先前采用 double 的完整临时候选只保留为方案验证材料，不作为本轮生产实施规则。若将来选择原字符串，只替换整数形数值越界后的结果，不把正常小数改成字符串。当前下一组改为不决定回退类型的共同依赖：trim、split_nonempty、try_convert_integral_double。最后一个函数只报告整数形 double 是否成功转换为 int64；失败返回 false 且不修改 out，失败后的 PropertyValue 类型仍待决。

边界以解析后的 double 为准。不能把 int64 最大值转 double 后当包含上界，因为 INT64_MAX 会舍入到 2^63。`9223372036854775807` 文本也可能先经 strtod 舍入为 2^63，先前 double 方案的候选据此保留 double，当前仍未选定方案。大于 2^53 的整数精度损失仍与参考 double-first 路径一致，本轮不增加精确整数解析器。

本轮完整实现指导覆盖 PropertyValue cpp 内部 trim、split_nonempty 与安全整数形 double 转换 helper。std::string 没有参考的 trim/split 成员，Property cpp 的匿名 helper 不能跨 cpp 调用，故明确给出 cpp 私有定义。try_convert_integral_double 是对参考 property_ex.cpp:130-133 数值分类/转换步骤的内部抽取，并加合法转换区间判断；没有新建回退策略、Manager 或拥有型包装。

parse_to_property_value、trans_parson_to_property_value、public static PropertyValue::create_from_string 的接口、步骤和调用关系已核对。完整树写入函数在越界结果选定后再闭合，本轮不提供带默认回退结果的生产实施指导。

参考数组分支直接修改 root[key] 并返回空 array 标记；路径转换不能用这个空标记覆盖已建数组。构树遍历 Property::maps()，重复文本键先覆盖，重复 levels[] 只保留最后一个文本项。保留参考的 [] 包含判断、按逗号分拆及嵌套数组的空键副作用，不新建 JSON 或确定遍历顺序的合同。

临时完整候选位于 `/tmp/qlog-config-tree-guide-idks4fyz`，Debug/Release 严格编译/运行及专门 float-cast-overflow 检查通过：修正版 helper、层级标量、数组、重复项/EOF、空根、字符串所有权/转义、参考数组副作用、2^63 边界和 double-first 舍入。不是生产 create_from_string 验收、完整 sanitizer、BQLog 双库差分、Windows、运行态/Buffer 或性能验收。

本轮提供的共同依赖候选另在同一临时目录完成 Debug/Release 严格编译/运行与 float-cast-overflow 检查：空白 trim、原空项 split、合法整数、负下界、正上界、超范围、非整数及非有限输入；失败输出保留原值。这组不决定配置节点的回退类型，也不计作生产函数验收。

后续由用户修正剩余 helper 并填写共同依赖 → 越界方案明确后完成三个树转换函数 → 实际生产审查/编译/链接/行为收口 → 配置 CMake 接入 → 过滤 helper/运行态参数 → Buffer 集成。SIMD 仍暂缓。

## 2026-10-06 第一轮源码审查（历史快照）

QLog 与 BQLog HEAD 仍为上列版本；两树仍有未提交修改。BQLog property.cpp/property_ex.cpp/property_ex.h 与参考 HEAD 无差异，不代表参考整树干净。

用户已修正 parse 的空 while 与等号分隔符，并填写 PropertyValue::serialize。当前生产仍存在四处问题，助手未改生产源码：

- Property::parse 的回退分隔符仍为 `.`，应为 `:`；`a:1` 返回零项，`log.level:info` 被误拆为 `log` 与 `level:info`。
- serialize_recursive 的数组分支误写成 `!object.is_object()`，所有标量都被当作空数组输出；应为 `object.is_array()`。
- 对象叶子行结尾追加了 `new_key + '\n'`，应只追加换行。
- 序列化遗漏 integral_type 分支，需按参考用 32 字节缓冲与 PRId64 输出 int64。

生产两个 cpp 通过 Debug/Release C++20 严格语法检查及独立调用方链接。实际探针显示两种配置均输出 `a=[]a\n`，标量整数输出 `[]`；当前生产解析/树序列化行为不通过。

临时目录 `/tmp/qlog-config-review-20261006-pnh0ws85` 中的四处修正版通过两种配置严格编译及解析、续行/EOF、load 合并、完整树序列化定向检查。这些结果只属于临时副本，没有生产配置 CTest 或 CMake 接入。

下一组指导覆盖六个 cpp 内部值识别函数：parse_boolean_value、is_decimal、parse_number_value、parse_null_value、parse_array_key、parse_array_value。指导候选已在临时调用方通过 Debug/Release 严格编译与定向运行检查，不涉及 double 转 int64 或树写入。函数名、参数名、布尔/null/数值前缀识别、数组 [] 查找规则按参考实际含义保留；生产定义由用户完成。

create_from_string 的整数越界结果仍待用户选择，先前的 double 保留提案没有被记录为接受。后续先复核上述修正和值识别组，再完成 parse_to_property_value、trans_parson_to_property_value、create_from_string；随后配置 CMake/行为收口，再进入过滤 helper 和运行态参数。SIMD 仍暂缓。

## 已确认的末尾续行规则

用户明确选择：跳过末尾未完成的键值项，保留其他有效项。
参考 src/bq_common/utils/property.cpp:73-82 在值以反斜杠结束、没有下一条保留行时无法退出循环。
QLog parse 指导在消费下一行前检查边界；不存在下一行则标记当前项未完成并跳过，不清空之前的结果，也不删除 load 前已有的键值。
若同名键先前有完整项、末尾新项未完成，完整项仍保留。

## 其他正常规则及安全边界

正常输入继续按参考：CRLF 转 LF、删除全部 tab、分行先丢弃原空项再 trim、首字符 #/! 的行为注释、先找等号再找冒号、分隔符只看前一个字符是否为反斜杠、初始空值跳过、续行 trim 后直接拼接且不重新判注释、只对值依次反转义\n/\=/\:。
参考续行拼接后可能变成空串，后续读取 value[size-1] 越界。指导使用非空条件保护末字符读取：空串时停止续行，沿后续流程保存空字符串；不重复执行初始空值过滤。这是另一个明确说明的安全修正，参考在该输入上没有正常可对照结果。
parse 保留重复项；load 依次 set，合并已有表，返回最终表是否非空。配置表/树不承诺任意文本无损往返，std::unordered_map 遍历顺序不视为与 bq::hash_map 相同。

## 2026-10-05 生产状态与验证边界（历史快照）

PropertyValue 已由用户填写。助手只同步已实现删除接口、Property::get 的声明/定义/参数拼写及 serialize 的字符串转义笔误，保留 CR。
Property 的 set/get/serialize、四个 cpp 私有辅助函数、load 与 parse 均已由用户填写。本轮核对 parse 存在两处抄写错误，尚未修改生产源码：find_split 使用了 `-`/`.`，应为 `=`/`:`；续行 while 的循环体为空，实际消费下一行的代码位于循环外。
生产 Property/PropertyValue cpp 均通过 Debug/Release C++20 严格语法检查；实际 Property 调用方编译链接通过，但正常 `a=1`/`b:2` 均返回零项，普通值会错误拼接下一行。空 while 在 Debug 定向检查中于 1.5 秒超时，Release -O2 表现不同，不能据此认为循环正确。
本轮仍不将配置 cpp 接入 CMake，也不将这些失败复现计为配置行为通过；已实现函数之外的生产定义继续由用户填写。

完整候选 parse 与当前 Property cpp 的临时副本在 /tmp/qlog-property-parse-guide-0qs_4367 中完成 Debug/Release 严格编译和定向运行检查：规范化、分隔符优先、完整续行、续行注释、空行、转义、末尾未完成项、同名旧值保留、拼接为空、load 合并及 serialize。
该验证仅证明指导候选与当前 helper/load 的衔接，不计入生产 CTest，不是 BQLog 双库差分、运行态/Buffer/完整系统验收。此前 Layout 10/10 不能计作配置模块通过。

## 本轮指导验证与后续顺序

本轮临时目录为 `/tmp/qlog-property-review-5lprjmtr`。仅在临时副本纠正两个 parse 抄写错误，Debug/Release 严格编译和定向运行检查均通过：正常分隔符及优先级、重复项、CRLF/tab/注释、完整续行及续行中的注释/键值文本、空行、转义、末尾未完成项跳过、旧同名项保留、拼接为空、load 合并和 serialize。生产 parse 未获此通过结论。

已核对 BQLog `property_ex.cpp` 与 `property_ex.h`，两文件与参考 HEAD 无差异。`create_from_string` 经 Property 哈希表构树，不按 key_list 顺序；类型解析采用 strtod 数值前缀、true/false/null 前缀，负数与正号开头回退为字符串。同名文本键先在表中覆盖，包括重复的 `key[]`。不应擅自改成 JSON 或完整数值字符串验证。

参考 `property_ex.cpp:130-133` 对超出 int64 范围的整数形 double 直接转 int64，存在未定义行为。助手已提出仅对此边界保留 double 或原字符串的选择，尚待用户决定；不将提案记录为已接受规则。

不依赖该数值选择的 `PropertyValue::serialize()` 可先实施。完整指导候选仅在临时头/cpp 副本中完成 Debug/Release 严格编译和运行检查，覆盖 null/bool/int64/decimal/string、空及混合数组、对象路径展开、转义、空对象和 invalid；不承诺哈希遍历顺序或任意树无损往返。浮点按参考保留 `%lf`、32 字节缓冲及其精度/截断特性。以上不是生产配置 CTest 或 BQLog 双库差分。

后续：先纠正并复核生产 parse → 用户填写 PropertyValue::serialize → 确认数值越界结果后给出并实施 create_from_string 完整指导 → 配置 CMake/行为收口 → 配置过滤 helper/运行态参数 → Buffer 集成。SIMD 已按当前对话暂缓。
