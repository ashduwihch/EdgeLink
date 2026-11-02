# EdgeLink 崩溃调试流程

## 工具分工

- **GDB**：观察源码行、调用栈、局部变量和线程。
- **Core Dump**：保存进程崩溃时的内存与寄存器快照，供事后离线分析。
- **strace**：记录系统调用、返回值、耗时和信号，适合定位文件、网络、权限与阻塞问题。

## 构建带调试符号的演示程序

故障演示程序与 `edgelinkd` 隔离，默认不会参与生产构建：

```bash
cmake -S . -B build-debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DEDGELINK_BUILD_DEBUG_TOOLS=ON
cmake --build build-debug -j4
```

`Debug` 构建保留源码行和变量信息；`-fno-omit-frame-pointer` 让调用栈更容易还原。

## 一键完成演练

```bash
./scripts/run_crash_debug_demo.sh
```

脚本会在 `build-debug/debug-artifacts/` 生成：

- `live-gdb-backtrace.txt`：GDB 捕获崩溃现场时的调用栈；
- `edgelink.core`：崩溃进程快照；
- `core-backtrace.txt`：重新启动 GDB 后从 core 恢复的调用栈；
- `strace.log.*`：进程终止前的系统调用和信号记录。

演示脚本使用 GDB 的 `generate-core-file`，不会修改 `/proc/sys/kernel/core_pattern`，
也不依赖 Ubuntu Apport 是否接管系统 Core Dump。

## 手动使用 GDB

```bash
gdb --args build-debug/edgelink_crash_demo --segfault
```

进入 GDB 后常用命令：

```text
run                 启动程序
backtrace           显示完整调用栈，可简写为 bt
frame 1             切换到第1号栈帧
list                 显示当前源码
info locals          查看局部变量
print variable       打印指定变量
thread apply all bt  显示所有线程的调用栈
quit                 退出
```

本例应当看到以下调用链：

```text
writeThroughNullPointer
crashWorker
runSegmentationFaultDemo
main
```

## 离线分析 Core Dump

离线分析必须同时提供“产生 core 的同一份可执行文件”：

```bash
gdb build-debug/edgelink_crash_demo \
    build-debug/debug-artifacts/edgelink.core
```

然后执行：

```text
bt
frame 0
list
info locals
```

如果二进制重新编译过，地址和调试符号可能不再对应原 core，所以发布时应保存相应版本的
可执行文件、动态库和符号文件。

## 使用 strace

记录进程、线程、时间和系统调用耗时：

```bash
strace -ff -tt -T -s 128 \
    -o build-debug/debug-artifacts/strace.log \
    build-debug/edgelink_crash_demo --abort
```

重点关注：

- 返回 `-1` 的系统调用及其 `errno`；
- `openat`、`connect`、`recvfrom`、`sendto`、`epoll_wait` 等调用；
- 单次调用的 `<耗时>`；
- 最后的 `SIGSEGV`、`SIGABRT`、`SIGTERM` 等信号。

## 当前虚拟机的 Core Dump 状态

检查命令：

```bash
ulimit -c
cat /proc/sys/kernel/core_pattern
```

本次检查结果是 `ulimit -c` 为 `0`，并且 Ubuntu Apport 接管了 `core_pattern`。
这意味着直接运行崩溃程序时，当前目录通常不会出现名为 `core` 的文件。
调试演示优先使用项目脚本；若要修改系统级 Core Dump 策略，应单独评估磁盘空间、
敏感数据和生产环境权限，不在项目脚本中自动修改。
