ifneq ($(KERNELRELEASE), )	
	scull-objs := main.o
	obj-m := scull.o

else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.kr *.mod.* .*.cmd *.o *.tmp
endif