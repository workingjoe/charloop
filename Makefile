ifneq (${KERNELRELEASE},)

	obj-m  = charloop.o

else
	KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
	MODULE_DIR := $(shell pwd)

.PHONY: all
all: modules

.PHONY: modules
modules:
	${MAKE} -C ${KERNEL_DIR} M=${MODULE_DIR}  modules

.PHONY: clean,
clean:
	rm -f *.o *.ko *.mod.c .*.o .*.ko .*.mod.c .*.cmd *~ *.ko.unsigned
	rm -f Module.symvers Module.markers modules.order
	rm -rf .tmp_versions .cache.mk

endif
