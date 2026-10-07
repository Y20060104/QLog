# 生产参数序列化到 Layout 组合测试（2026-10-03）

本轮仅补测试、测试构建配置和验证记录，未修改生产源码。

## 新增三组

- serialization_mixed：使用生产 make_size_seq<true>/fill_arguments 生成所有整数宽度、bool、char/char16/char32、nullptr、指针、float/double 参数；通过 UTF-8/UTF-16 格式串调用公开 do_layout。检查实际对象指针存储值和固定12字节槽位，检查空参数包。
- serialization_strings：string/view、u16string/view、u32string/view、空字符指针、内嵌NUL、10000字节参数及格式说明。检查UTF-32参数tag为UTF-16和转换后的字节长度。循环0～65长度，UTF-8与UTF-16字符串后混排64位和8位整数，两种格式串均执行，检查跨4字节边界的参数消费。
- serialization_custom：成员chars、成员UTF-16回调、ADL chars、ADL回调；检查回调接收字节数、填充时只调用一次、Layout不再次调用、零长度回调跳过、内嵌NUL以及后续数值参数。

序列化输出前后各有16字节哨兵，用于检查超出测量区域的写入。它不是完整越界读检测或sanitizer。记录头和扩展信息仍由测试夹具构造，不经过尚未完成的生产日志入口。

## 执行证据

构建目录：/tmp/qlog-serialization-layout-20261003-39mtm8y8；其中保存每种配置的configure/build/test日志，临时目录不保证永久保留。

```sh
cmake -S /home/qq344/QLog -B <build-dir> -DCMAKE_BUILD_TYPE=<Debug或Release> -DCMAKE_CXX_FLAGS=-Werror
cmake --build <build-dir> -j 2
ctest --test-dir <build-dir> --output-on-failure
```

Debug和Release严格构建均通过，各10/10：9个Layout测试分组（包含本轮3组）和1个既有smoke。Release检查不依赖assert。未发现需要修改生产代码的新问题。

范围不包括BQLog双库差分、SIMD、local/DST、性能、并发、指数异常宽度、NaN/Inf、超长线程名或极端容量。custom四字节字符的未决合同没有改变，也未作为已支持路径测试。
