obj-m += dm_race_detector.o

KVER    ?= $(shell uname -r)
KDIR    ?= /lib/modules/$(KVER)/build
MDIR    := $(shell pwd)

.PHONY: all install load unload clean
all:
	$(MAKE) -C $(KDIR) M=$(MDIR) modules

load: all
	sudo modprobe dm-mod 2>/dev/null || true
	sudo insmod ~/test_traid/dm_race_detector.ko

unload:
	sudo dmsetup remove_all 2>/dev/null
	sudo rmmod ~/test_traid/dm_race_detector.ko

clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean

test: load
	sudo bash ./test.sh
