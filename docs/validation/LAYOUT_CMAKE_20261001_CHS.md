# Layout / UTF CMake 接入记录（2026-10-01）

## 变更与来源

- QLog：feat/spsc-ring-opt，HEAD 94fefc0f29b21a1dbdd258fdb5725aadf84d770e；现有未提交修改、删除和草稿保留，未提交 Git commit。
- BQLog：78cbfbef4f87e7558335f0c04bf6dd6894b46451；本轮采用的 layout.h/.cpp 与 util.cpp 相对 HEAD 无差异，整树非干净。
- CMake 为 qlog 增加 src/layout/layout.cpp、src/utility/utf_conversion.cpp；原有四个编译单元和 PUBLIC Debug 宏保持原样。
- 参数名与扩容决定见 [补充决策](../decisions/LAYOUT_ALIGNMENT_20261001_CHS.md)。本轮未修改生产 cpp/hpp，未删除 CR。

## 实际执行

证据目录：/tmp/qlog-layout-cmake-20261001-li07mk24（临时目录，不保证永久保留）。其中 Debug-0.log、Debug-1.log、Release-0.log、Release-1.log 为配置/构建日志，各构建目录保存 compile_commands.json。

每种配置执行：

```sh
cmake -S /home/qq344/QLog -B <build_dir> -DCMAKE_BUILD_TYPE=<Debug或Release> -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build <build_dir> -j 2
```

| 项目 | Debug | Release |
|---|---|---|
| CMake 配置 | 通过 | 通过 |
| qlog 静态库构建 | 通过，有下述警告 | 通过，有下述警告 |
| 现有 smoke 可执行文件构建/链接 | 通过，未运行 | 通过，未运行 |
| Layout、UTF 和 smoke 编译命令的 QLOG_DEBUG | 定义 | 未定义 |

编译使用项目既有 -Wall/-Wextra/-Wpedantic/-Wconversion/-Wshadow，未增加 -Werror。两种配置均有已知警告：layout.cpp:212 空的 expand_format_content_buff_size 的 required_size 参数未使用。没有用消警代码掩盖未实现状态。

## 验证边界

没有运行 ctest 或任何行为测试。没有新 Layout 调用方完整链接验证；静态库和只使用版本接口的 smoke 构建成功不能证明 do_layout 已定义或可调用。解析模板尚未由完整正文扫描器实例化。UTF 转换、布局差分、SIMD、性能、Windows 本轮均未验收。

下一步由用户依指南填写扩容与基础追加函数，再做相应严格编译及调用方链接。当前构建结果不得写成完整 Layout 实现完成。
