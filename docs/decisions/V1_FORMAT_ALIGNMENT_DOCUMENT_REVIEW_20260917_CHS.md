# 2026-09-17 format对齐与剩余V1指南文档核验

范围：源码只读审计、格式合同与指南修订、文档静态核验。没有修改生产源码、测试或CMake，没有配置/编译/运行测试，没有benchmark。

源码基准：BQLog 60ef4d3ea52635dc54e87e79dfb3a71fb5b1ecc9；QLog 58b6948c33bb2f4b94db7c3e3d77b228eaf5afd7加当前未提交修改。权威树 /home/qq344/QLog。

## 审阅内容

- 沿BQLog Producer→copy/hash→worker→Appender→layout UTF-8 scanner追踪，区别零参数与参数耗尽、宽松spec与默认类型表示；示例是静态推导，未运行兼容测试。
- 核对当前QLog Producer不解析、配置A真实存在、诊断B部分实现与三处接线问题，以及formatter/worker/Appender尚无实现。
- ADR-016成为format唯一合同；ADR-010保留hash/wire，ADR-014/015和活动指南同步，历史指南加覆盖说明，验收历史不重写。
- 独立复审纠正安全数值扩展规则、管理/attach在worker失败后的终结条件、旧cache残留和阶段顺序。

## 静态检查

相对Markdown链接、章节锚点和代码围栏已核对；生产include/src/tests与根CMake共232个文件使用SHA-256前后比较。最终检查结果以本次写回核验输出为准；源码和测试指纹必须全部相同。

检查脚本及精确JSON结果保存在Windows参考工作区 project_design/qlog_v1_format_alignment_20260917/validate_docs.py、validation.json、before.json，供复核；这些是文档验证材料，不是生产测试。

## 当前下一步

B0三处诊断接线修复→B闭合→C worker formatter→D Appender/batch→E邮箱→F worker/Session/runtime→G/H公共API/关停/示例。依照V1_REMAINING_IMPLEMENTATION_GUIDE_CHS.md和已同步的V1_FINISH_IMPLEMENTATION_GUIDE_CHS.md实施。

V1仍未完成；formatter输出兼容、运行并发、故障恢复和性能均须后续真实实现与验收。不能用本报告声称生产已通过或吞吐优于BQLog。
