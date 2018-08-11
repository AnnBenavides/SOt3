ccflags-y := -Wall -std=gnu99

obj-m := syncread.o
syncread-objs := kmutex.o syncread-impl.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

#include $(KDIR)/.config

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

syncread-impl.o kmutex.o: kmutex.h

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
