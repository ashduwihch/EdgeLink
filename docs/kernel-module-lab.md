# EdgeLink 内核模块入门实验

这个实验先在 Ubuntu 虚拟机上理解 Linux 内核模块的生命周期。计划中的
“RK3568 可加载/卸载”验收需要开发板在手后再做，虚拟机结果不能代替板端结果。

## 先看代码

`drivers/hello_module/edgelink_hello.c` 中有两个函数：

- `edgelink_hello_init`：模块加载时执行，向内核日志写一条消息。
- `edgelink_hello_exit`：模块卸载时执行，再写一条消息。

`module_init` 和 `module_exit` 把这两个函数登记给内核。`MODULE_LICENSE("GPL")`
说明模块的许可证；它不是普通 C 程序的 `main()`。

## 在虚拟机上亲手操作

先确认当前内核有对应的构建目录：

```bash
uname -r
ls -ld /lib/modules/$(uname -r)/build
```

再编译并查看生成文件：

```bash
make -C drivers/hello_module
file drivers/hello_module/edgelink_hello.ko
modinfo drivers/hello_module/edgelink_hello.ko
```

确认文件存在后，加载并检查日志：

```bash
sudo insmod drivers/hello_module/edgelink_hello.ko
lsmod | grep '^edgelink_hello'
sudo dmesg | tail -n 20
```

最后卸载，确认退出消息：

```bash
sudo rmmod edgelink_hello
sudo dmesg | tail -n 20
```

`insmod` 和 `rmmod` 会修改正在运行的内核状态，操作时只针对这个实验模块。
模块加载成功但日志里没有预期消息时，先看 `dmesg` 的具体错误，不要反复加载。

## 开发板在手后

记录开发板的 `uname -r`，取得**与板端正在运行的内核完全匹配**的源码、配置和
构建产物，再用板端内核构建目录编译模块。仅把虚拟机的 `.ko` 复制到 RK3568
通常不能加载：架构、内核版本及配置都必须匹配。板端成功执行加载、查看日志、卸载后，
才能把计划中的 RK3568 验收标记完成。
