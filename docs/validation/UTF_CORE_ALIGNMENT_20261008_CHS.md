# UTF 通用核心与 Layout 专用扫描对齐记录

日期：2026-10-08。用户授权助手修改代码：“通用核心是核心，其他需要专用的再修改为专用的”。本记录优先于先前保留 UTF-16 原始字节 utility 接口的决定和仅指导阶段快照。

## 源码依据与修改起点

QLog 为 feat/spsc-ring-opt@f24f3afcd8e9418ea48ad5a6b05680c622008145，已有未提交生产草稿和文档；未清理或提交。BQLog 为 main@78cbfbef4f87e7558335f0c04bf6dd6894b46451，整树仍有 MISO/构建等本地修改；采用的 util.h、util.cpp、layout.cpp、win64_misc.cpp 相对 HEAD 无差异。

参考通用软件核心是 util.cpp:653-692；util::utf16_to_utf8_sw:1850 只是测试转调，生产入口:1506负责路径选择。Layout 参数字符串在layout.cpp:1120-1128按字节长度适配后调用通用入口。专用扫描在:173-195，格式串主循环从:863开始，遇非ASCII或括号后继续专用处理。

修改起点用户已经把QLog的src改成char16_t*，内部仍按字节数除二，read_unit对typed指针还乘sizeof(char16_t)，Layout也仍传char*和字节数量。保存起点快照后，本轮把类型、单位、读取方式及调用方一起收齐。

## 最终实现与职责

- utility/utf_conversion.hpp和cpp：const char16_t* src、src_character_num、char* dst、dst_character_num。输入计UTF-16码元，输出容量和返回值计UTF-8字节。采用参考src/src_end/dst_ptr直接读取、ASCII连续处理及位运算；移除字节适配读取、内部除二和最坏容量断言。容量参数按参考保留但不使用，调用方保证空间，不按容量截断，不追加终止符，嵌入零正常转换。
- 孤立低代理项跳过；未配对高代理项丢弃，不消费后面的普通码元；合法代理对输出四字节。这些都是参考核心已有规则，不新增替换字符或错误协议。
- 明确保留QLog既有零长度提前返回、非零指针assert和char16_t大小static_assert。零长度可传空指针，避免参考中的空指针加法/相减。非零输入须有效、满足char16_t对齐，源目标不重叠，输出空间足够。
- Layout::insert_str_utf16保留原始字节str/len，调用点使用reinterpret_cast<const char16_t*>(str)和len >> 1U。扩容、按实际写入量推进及CR保留。
- find_brace_and_convert_u16_sw留在Layout匿名命名空间，按参考改为start/end指针扫描及对应标志返回；非ASCII或花括号使其停止并交还控制权。python_style_format_content_utf16已经符合专用职责，函数体原样保留。
- 合法记录路径中，RecordHeader按8字节对齐、大小40，格式数据紧随其后；参数区及步长按4字节对齐，UTF-16字符串内容位于参数起点后8字节，满足2字节读取对齐。任意外部Record指针不会因此获得自动对齐保证。
- 后续Windows调用同一通用接口；没有复制平台特有算法或新增Manager/detail/拥有型包装。SIMD仍后置，尚未填写的utf8_to_utf16_sw仍由用户完成。

## 实际构建与回归

根target已编译utility cpp，根CMake未改，生产仍14个cpp。新增tests/utf_conversion_test.cpp并接tests/CMakeLists.txt，常驻CTest由10项变为11项。

直接接口16种情况覆盖码元数量、精确实际输出容量、ASCII/长ASCII、嵌入零、花括号作为普通内容、BMP/补充平面、代理对和孤立/连续代理项、无终止符与两侧哨兵。

外部临时CMake调用方add_subdirectory实际QLog并链接实际QLog::qlog，另外加入BQLog软件核心源码摘录对照。Debug/Release全警告及-Werror构建链接通过，CTest各12/12（常驻11+临时对照1）通过。编译数据库核对库、常驻UTF调用方、临时对照调用方的PUBLIC宏一致：Debug定义QLOG_DEBUG且无NDEBUG；Release不定义QLOG_DEBUG且有NDEBUG。

参考对照覆盖65536种单码元值，以及15种边界码元的两两组合加普通后缀225组，共65761组，数量和整个目标缓冲内容一致。参考是重新摘录的当前软件核心，不是完整BQLog双库或SIMD差分。

ASan+UBSan对实际库和调用方定向7/7通过：utility接口/参考对照，以及Layout扫描、参数和三个序列化组合分组。未运行的其他sanitizer分组不记为通过。本轮不计Windows原生、SIMD、完整异步日志、Ring/MISO并发恢复或性能验收；平台I/O未因此接入根库。

## 范围与复核材料

生产仅修改include/qlog/utility/utf_conversion.hpp、src/utility/utf_conversion.cpp、src/layout/layout.cpp；测试仅修改上述测试cpp及tests/CMakeLists.txt。CR行逐项保留，格式串主函数正文一致；其他include/src/tests及根CMake的SHA-256保持修改起点值。

审计目录：/tmp/qlog-utf-core-alignment-20261008-yi_pb1bt。保存修改前源码、修改前/验证时清单、参考摘录、临时调用方、实际编译/CTest日志和宏核对结果。测试列举会重写CTest的LastTest.log，实际结果以单独保存的Debug/Release/Sanitized-ctest.log为准。旧UTF-8指导81/117项仅属于对应历史接口快照。
