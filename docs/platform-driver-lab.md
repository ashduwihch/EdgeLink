# Platform Driver 与设备树入门实验

本实验使用 Ubuntu 虚拟机中的**假 Platform Device**，学习设备与驱动匹配、
`probe()` / `remove()` 生命周期，以及通过 `/dev` 向用户态暴露测试节点。
它不访问真实寄存器，也不代表 RK3568 设备树已经验证。

## 先弄清三个角色

- Platform Device：告诉内核“这里有一个设备”。本实验由模块代码临时创建。
- Platform Driver：告诉内核“我能管理哪类设备”，并提供 `probe()`、`remove()`。
- Device Tree：在真实开发板上描述板级硬件。虚拟机实验没有加载设备树节点。

本实验的匹配过程是：

```text
加载 .ko
  → 注册 Platform Driver，名字为 edgelink-platform-demo
  → 注册同名的假 Platform Device
  → Platform Bus 匹配成功，调用 probe()
  → probe() 注册 miscdevice，出现 /dev/edgelink_platform_demo
  → 用户态 cat 读取固定文本
  → 卸载模块，先移除设备并调用 remove()，再注销驱动
```

`drivers/platform_demo/edgelink_platform_demo.c` 还声明了
`compatible = "edgelink,platform-demo"`。这是**未来真实设备树匹配的接口示例**；
虚拟机此次匹配依靠的是设备名与 `driver.name` 相同，不是 `compatible`。

## 虚拟机上亲手验证

在项目根目录执行：

```bash
make -C drivers/platform_demo
modinfo drivers/platform_demo/edgelink_platform_demo.ko
sudo insmod drivers/platform_demo/edgelink_platform_demo.ko
lsmod | grep '^edgelink_platform_demo'
ls -l /dev/edgelink_platform_demo
cat /dev/edgelink_platform_demo
sudo dmesg | tail -n 20
```

正常时，`cat` 应输出 `EdgeLink platform demo: probe succeeded`；`dmesg`
应出现 `probe: created /dev/edgelink_platform_demo`。

**先让 `cat` 命令结束，再卸载模块**：

```bash
sudo rmmod edgelink_platform_demo
ls -l /dev/edgelink_platform_demo
sudo dmesg | tail -n 20
```

卸载后 `ls` 提示节点不存在是预期结果，`dmesg` 应出现 `remove` 日志。
如果 `insmod` 失败或节点没出现，先查 `dmesg`，不要反复加载。
加载/卸载 `.ko` 会改变正在运行的内核状态，仅在实验虚拟机执行。

## 与 RK3568 真机的区别

真实设备一般在板级设备树中描述，例如以下**示意片段**：

```dts
demo {
    compatible = "edgelink,platform-demo";
    status = "okay";
};
```

开发板内核读取设备树后创建 Platform Device，驱动通过 `compatible` 匹配。
此时**不应同时注册本实验的假设备**，否则同名 `/dev` 节点可能发生冲突。
要做真机实验，需要按实际 RK3568 板卡设备树、内核版本和交叉编译环境
另写板端版本，并补充设备树部署及实机验证。当前只完成虚拟机练习。
