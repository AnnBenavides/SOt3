ccflags-y := -Wall -std=gnu99

obj-m := vigia.o
vigia-objs := kmutex.o vigia-impl.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

#include $(KDIR)/.config

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

vigia-impl.o kmutex.o: kmutex.h

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
