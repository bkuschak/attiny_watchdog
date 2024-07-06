# Tested on 5.10.168-ti-r72 Debian 12 on AM335x.

obj-m += attiny_wdt.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

install:
	# NOTE we have to override KERNELRELEASE because our running kernel has an EXTRAVERSION that the build/Makefile doesn't have...
	# otherwise the module is installed into the wrong directory
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) KERNELRELEASE=$(shell uname -r) modules_install
	depmod -a
	#update-initramfs -u

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
