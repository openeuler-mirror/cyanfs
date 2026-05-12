.PHONY:all
all: linux linux-demo utils

.PHONY:linux
linux:
	$(MAKE) -C linux

.PHONY:utils
utils:
	cmake -B build/utils utils -DCYANFS_DEBUG=$(CYANFS_DEBUG)
	$(MAKE) -C build/utils

.PHONY:linux-demo
linux-demo:
	cmake -B build/demo linux/demo
	$(MAKE) -C build/demo

.PHONY:debug
debug:
	$(MAKE) CYANFS_DEBUG=1 all
