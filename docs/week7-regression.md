# Week 7 虚拟机回归记录

记录日期：2026-11-02。此记录对应计划中的 10/12—10/18 工作，
**不等于 RK3568 真机验收或 v0.7.0 发布声明**。

## 已验证

- 用户态工程：`cmake -S . -B build` 和 `cmake --build build -j4` 成功。
- CTest：在允许创建本地 socket 和伪终端的环境中，从 `build/` 执行
  `ctest --output-on-failure`，29 项测试中 28 项通过、1 项跳过、0 项失败。
  跳过的是 `SocketCanTransportTest`，因为虚拟机没有 `vcan0`。
- `SystemMonitorTest` 通过；覆盖 `/proc/stat` 两次差值、内存字节换算和
  thermal zone 温度解析。
- `drivers/hello_module/edgelink_hello.ko` 已在虚拟机编译，并由用户执行
  加载/卸载实验；内核日志中出现模块加载与卸载消息。
- `drivers/platform_demo/edgelink_platform_demo.ko` 已在虚拟机编译；用户执行
  `insmod` 后，`cat /dev/edgelink_platform_demo` 得到预期文本，并反馈已卸载。
- GDB/Core Dump 演示已能定位 `tools/crash_demo.cpp` 中的空指针写入；
  `strace` 演示脚本可用于观察信号终止流程。

## 回归中发现并修复

`ModbusRtuTransportTest` 曾偶发超时：测试在从站读取线程启动前关闭了
伪终端从端，主端可能提前遇到 `EIO`。现在保持从端打开直到通信结束，
修复后完整 CTest 回归通过。

## 尚未完成

- 10/14 ASan、UBSan、Valgrind 内存检查：按当前学习安排暂缓。
- 10/15 perf 性能分析与热点结论：按当前学习安排暂缓。
- RK3568 上的 `.ko` 加载/卸载、真实设备树匹配及 `/dev` 节点验证：
  开发板不在手边，虚拟机假设备不能替代真机结果。
- `vcan0` / 真实 CAN 收发：本次环境没有对应接口。

因此本次适合提交为 Week 7 阶段性开发成果；若按原计划验收 v0.7.0，
上述待补项需要明确标注，不能把整周完成标准写成“全部通过”。
