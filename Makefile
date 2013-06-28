obj-m := loggermodule.o
KDIR=/home/wentianc/develop/1262-thor/out/target/product/thor/obj/KERNEL_OBJ/
PWD := $(shell pwd)

loggermodule-objs := cpufreq.o logger.o procstat.o

all:
	make -C $(KDIR) ARCH=arm CROSS_COMPILE=/disk/CodeSourcery/Sourcery_G++_Lite/bin/arm-none-linux-gnueabi- SUBDIRS=$(PWD) modules

clean:
	make -C $(KDIR) ARCH=arm CROSS_COMPILE=/disk/CodeSourcery/Sourcery_G++_Lite/bin/arm-none-linux-gnueabi- SUBDIRS=$(PWD) clean
