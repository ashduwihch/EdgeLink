# 将示例 Platform Driver 编译为独立的 .ko，不纳入用户态 CMake 工程。
obj-m += edgelink_platform_demo.o

# 虚拟机默认使用当前内核的头文件；交叉编译时可显式传入 KDIR。
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
