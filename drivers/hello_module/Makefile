# 让内核构建系统把 edgelink_hello.c 编译成 edgelink_hello.ko
obj-m += edgelink_hello.o

# 默认使用当前运行内核的构建目录；开发板上可显式传入 KDIR。
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
