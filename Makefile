# SPDX-License-Identifier: GPL-2.0
# Linux Kernel Module & User-Space Test Makefile

# Kernel module object
obj-m += telemetry_dev.o
telemetry_dev-objs := driver/telemetry_dev.o

# Add our custom include directory to kernel build flags
ccflags-y := -I$(src)/include

# Kernel build directory (defaults to running kernel)
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# User space compiler
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude

.PHONY: all modules test_app clean install load unload help

all: modules test_app

# Build the Linux Kernel Module (.ko)
modules:
	@echo "=================================================="
	@echo "  Building Linux Kernel Module: telemetry_dev.ko"
	@echo "=================================================="
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Build the User-Space Test Client
test_app: userspace_test/test_app.c include/telemetry_ioctl.h
	@echo "=================================================="
	@echo "  Building User-Space Test Tool: test_app"
	@echo "=================================================="
	$(CC) $(CFLAGS) -o test_app userspace_test/test_app.c

# Clean build artifacts
clean:
	@echo "Cleaning kernel module build artifacts..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -f test_app
	@rm -f *.o *.ko *.mod *.mod.c *.order *.symvers
	@rm -f driver/*.o driver/.*.cmd driver/*.mod driver/*.mod.c

# Load module into running kernel
load:
	@echo "Inserting telemetry_dev.ko..."
	sudo insmod telemetry_dev.ko
	@sudo dmesg | tail -n 5

# Unload module from running kernel
unload:
	@echo "Removing telemetry_dev.ko..."
	sudo rmmod telemetry_dev
	@sudo dmesg | tail -n 5

# Run the test app with root privileges
run_test: test_app
	@echo "Running test suite against /dev/telemetry_dev..."
	sudo ./test_app

help:
	@echo "Available Targets:"
	@echo "  make          : Build both kernel module and user-space test tool"
	@echo "  make modules  : Build only the kernel module (.ko)"
	@echo "  make test_app : Build only the user-space test client"
	@echo "  make load     : Insert module into running kernel (sudo insmod)"
	@echo "  make unload   : Remove module from running kernel (sudo rmmod)"
	@echo "  make run_test : Execute test suite against /dev/telemetry_dev"
	@echo "  make clean    : Remove all build outputs"
