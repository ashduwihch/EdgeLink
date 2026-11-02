#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

/**
 * @brief 模块被加载时运行一次，并向内核日志记录加载成功。
 * @return 返回0表示初始化成功，内核可继续加载模块。
 */
static int __init edgelink_hello_init(void)
{
	pr_info("edgelink_hello: module loaded\n");
	return 0;
}

/**
 * @brief 模块被卸载时运行一次，并向内核日志记录退出。
 */
static void __exit edgelink_hello_exit(void)
{
	pr_info("edgelink_hello: module unloaded\n");
}

module_init(edgelink_hello_init);
module_exit(edgelink_hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EdgeLink contributors");
MODULE_DESCRIPTION("Minimal EdgeLink kernel module for load/unload practice");
