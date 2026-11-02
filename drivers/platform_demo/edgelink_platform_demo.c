#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define EDGELINK_DEMO_NAME "edgelink_platform_demo"

/* 本实验的 /dev 节点只读，避免把模拟设备误当作真实硬件控制接口。 */
static const char edgelink_demo_message[] = "EdgeLink platform demo: probe succeeded\n";
static struct platform_device *edgelink_demo_device;

/**
 * @brief 用户态读取 /dev 节点时，返回一条固定的实验消息。
 * @param file 已打开的设备文件。
 * @param buffer 用户态接收缓冲区。
 * @param count 用户态缓冲区可用字节数。
 * @param offset 当前文件偏移量，确保读完后返回 EOF。
 * @return 成功读取的字节数；失败时返回负错误码。
 */
static ssize_t edgelink_demo_read(struct file *file, char __user *buffer,
				  size_t count, loff_t *offset)
{
	return simple_read_from_buffer(buffer, count, offset,
				       edgelink_demo_message,
				       sizeof(edgelink_demo_message) - 1);
}

static const struct file_operations edgelink_demo_fops = {
	.owner = THIS_MODULE,
	.read = edgelink_demo_read,
	.llseek = no_llseek,
};

static struct miscdevice edgelink_demo_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = EDGELINK_DEMO_NAME,
	.fops = &edgelink_demo_fops,
	.mode = 0444,
};

/**
 * @brief 设备与驱动匹配成功后，注册用户态可读取的 /dev 节点。
 * @param device 本次匹配到的 Platform Device。
 * @return 成功返回0；注册节点失败时返回内核错误码。
 */
static int edgelink_demo_probe(struct platform_device *device)
{
	int result = misc_register(&edgelink_demo_misc);

	if (result) {
		dev_err(&device->dev, "cannot create /dev/%s: %d\n",
			EDGELINK_DEMO_NAME, result);
		return result;
	}

	dev_info(&device->dev, "probe: created /dev/%s\n",
		 EDGELINK_DEMO_NAME);
	return 0;
}

/**
 * @brief 设备与驱动解除绑定时，注销对应的 /dev 节点。
 * @param device 即将解除绑定的 Platform Device。
 * @return 返回0表示清理完成。
 */
static int edgelink_demo_remove(struct platform_device *device)
{
	misc_deregister(&edgelink_demo_misc);
	dev_info(&device->dev, "remove: removed /dev/%s\n",
		 EDGELINK_DEMO_NAME);
	return 0;
}

/* 真机设备树可用 compatible 匹配；虚拟机的假设备按 driver.name 匹配。 */
static const struct of_device_id edgelink_demo_of_match[] = {
	{ .compatible = "edgelink,platform-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, edgelink_demo_of_match);

static struct platform_driver edgelink_demo_driver = {
	.probe = edgelink_demo_probe,
	.remove = edgelink_demo_remove,
	.driver = {
		.name = "edgelink-platform-demo",
		.of_match_table = edgelink_demo_of_match,
	},
};

/**
 * @brief 加载模块时注册驱动和假设备，触发一次真实的匹配与 probe。
 * @return 两者都注册成功返回0；失败时撤销已注册对象并返回错误码。
 */
static int __init edgelink_demo_init(void)
{
	int result = platform_driver_register(&edgelink_demo_driver);

	if (result)
		return result;

	/* 虚拟机没有对应的真实设备，用一个同名设备触发 Platform Bus 匹配。 */
	edgelink_demo_device = platform_device_register_simple(
		"edgelink-platform-demo", PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(edgelink_demo_device)) {
		result = PTR_ERR(edgelink_demo_device);
		platform_driver_unregister(&edgelink_demo_driver);
		return result;
	}

	pr_info("edgelink_platform_demo: module loaded\n");
	return 0;
}

/**
 * @brief 卸载模块时先移除假设备，再注销 Platform Driver。
 */
static void __exit edgelink_demo_exit(void)
{
	platform_device_unregister(edgelink_demo_device);
	platform_driver_unregister(&edgelink_demo_driver);
	pr_info("edgelink_platform_demo: module unloaded\n");
}

module_init(edgelink_demo_init);
module_exit(edgelink_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EdgeLink contributors");
MODULE_DESCRIPTION("Virtual Platform Driver and /dev node experiment");
