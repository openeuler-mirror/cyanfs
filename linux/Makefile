ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
export ROOT_DIR

ccflags-y := -DGIT_COMMIT=$(shell git -C ${ROOT_DIR} log --format=%H -n 1)
cyanfs-objs := vdisk.o main.o ctl.o $(patsubst ${ROOT_DIR}/%.c,%.o,$(wildcard ${ROOT_DIR}/../core/*.c))
obj-m := cyanfs.o

ifneq ($(CYANFS_DEBUG),)
	ccflags-y += -DCYANFS_DEBUG_ENABLED
	cyanfs-objs += configfs.o
else
	ccflags-y += -Werror
endif

ifeq ($(KERNELRELEASE),)

KDIR?=/lib/modules/$(shell uname -r)/build

release:
	$(MAKE) -C $(KDIR) M=$(ROOT_DIR) modules
	strip --strip-debug $(ROOT_DIR)/cyanfs.ko

debug:
	$(MAKE) -C $(KDIR) M=$(ROOT_DIR) CYANFS_DEBUG=1 modules

clean:
	$(MAKE) -C $(KDIR) M=$(ROOT_DIR) clean

endif
