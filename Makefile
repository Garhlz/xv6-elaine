# To compile and run with a lab solution, set the lab name in lab.mk
# (e.g., LAB=util).  Run make grade to test solution with the lab's
# grade script (e.g., grade-lab-util).

-include conf/lab.mk

.DEFAULT_GOAL := build

K = kernel
U = user
BUILD = build
KBUILD = $(BUILD)/kernel
UBUILD = $(BUILD)/user
MKFSBUILD = $(BUILD)/mkfs
NOTXV6BUILD = $(BUILD)/notxv6

KERNEL = $(KBUILD)/kernel
FSIMG = $(BUILD)/fs.img
GDBINIT = $(BUILD)/.gdbinit
MKFS = $(MKFSBUILD)/mkfs
PH = $(NOTXV6BUILD)/ph
BARRIER = $(NOTXV6BUILD)/barrier

KOBJS = \
  $(KBUILD)/entry.o \
  $(KBUILD)/kalloc.o \
  $(KBUILD)/string.o \
  $(KBUILD)/main.o \
  $(KBUILD)/vm.o \
  $(KBUILD)/proc.o \
  $(KBUILD)/swtch.o \
  $(KBUILD)/trampoline.o \
  $(KBUILD)/trap.o \
  $(KBUILD)/sysarg.o \
  $(KBUILD)/syscall.o \
  $(KBUILD)/syscall_table.o \
  $(KBUILD)/sysproc.o \
  $(KBUILD)/bio.o \
  $(KBUILD)/fs.o \
  $(KBUILD)/log.o \
  $(KBUILD)/sleeplock.o \
  $(KBUILD)/file.o \
  $(KBUILD)/pipe.o \
  $(KBUILD)/exec.o \
  $(KBUILD)/sysfd.o \
  $(KBUILD)/sysfile.o \
  $(KBUILD)/sysmmap.o \
  $(KBUILD)/sysnetcall.o \
  $(KBUILD)/kernelvec.o \
  $(KBUILD)/plic.o \
  $(KBUILD)/virtio_disk.o \
  $(KBUILD)/stats.o \
  $(KBUILD)/sprintf.o \
  $(KBUILD)/e1000.o \
  $(KBUILD)/net.o \
  $(KBUILD)/sysnet.o \
  $(KBUILD)/pci.o

KOBJS_KCSAN = \
  $(KBUILD)/start.o \
  $(KBUILD)/console.o \
  $(KBUILD)/printf.o \
  $(KBUILD)/uart.o \
  $(KBUILD)/spinlock.o

ifdef KCSAN
KOBJS_KCSAN += \
	$(KBUILD)/kcsan.o
endif

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX =

# Try to infer the correct TOOLPREFIX if not set.
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64
XV6TEST = GOCACHE=$(CURDIR)/$(BUILD)/go-cache go run ./tests/host/cmd/xv6test

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb

ifdef LAB
LABUPPER = $(shell echo $(LAB) | tr a-z A-Z)
XCFLAGS += -DSOL_$(LABUPPER) -DLAB_$(LABUPPER)
endif

CFLAGS += $(XCFLAGS)
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

CFLAGS += -DNET_TESTS_PORT=$(SERVERPORT)

ifdef KCSAN
CFLAGS += -DKCSAN
KCSANFLAG = -fsanitize=thread
endif

# Disable PIE when possible (for Ubuntu 16.10 toolchain).
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

$(BUILD) $(KBUILD) $(UBUILD) $(MKFSBUILD) $(NOTXV6BUILD):
	mkdir -p $@

image: $(FSIMG)

$(KERNEL): $(KOBJS) $(KOBJS_KCSAN) $(K)/kernel.ld $(KBUILD)/initcode | $(KBUILD)
	$(LD) $(LDFLAGS) -T $(K)/kernel.ld -o $@ $(KOBJS) $(KOBJS_KCSAN)
	$(OBJDUMP) -S $@ > $(KBUILD)/kernel.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(KBUILD)/kernel.sym

$(KOBJS): EXTRAFLAG := $(KCSANFLAG)

$(KBUILD)/%.o: $(K)/%.c | $(KBUILD)
	$(CC) $(CFLAGS) $(EXTRAFLAG) -c -o $@ $<

$(KBUILD)/%.o: $(K)/%.S | $(KBUILD)
	$(CC) $(CFLAGS) $(EXTRAFLAG) -c -o $@ $<

$(KBUILD)/initcode: $(U)/initcode.S | $(KBUILD)
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -I$(K) -c $< -o $(KBUILD)/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(KBUILD)/initcode.out $(KBUILD)/initcode.o
	$(OBJCOPY) -S -O binary $(KBUILD)/initcode.out $@
	$(OBJDUMP) -S $(KBUILD)/initcode.o > $(KBUILD)/initcode.asm

tags: $(KOBJS) $(UBUILD)/_init
	etags *.S *.c

# Native xv6 用户态 runtime：crt0 接住 exec() 传入的 argc/argv，
# 然后进入 main(argc, argv)，最后通过 exit() 返回内核。
XV6_ULIB = \
	$(UBUILD)/crt0_entry.o \
	$(UBUILD)/crt0.o \
	$(UBUILD)/ustring.o \
	$(UBUILD)/ufile.o \
	$(UBUILD)/ugetpid.o \
	$(UBUILD)/usys.o \
	$(UBUILD)/printf.o \
	$(UBUILD)/umalloc.o \
	$(UBUILD)/statistics.o

UPROGS = \
	$(UBUILD)/_cat \
	$(UBUILD)/_echo \
	$(UBUILD)/_find \
	$(UBUILD)/_forktest \
	$(UBUILD)/_grep \
	$(UBUILD)/_init \
	$(UBUILD)/_kill \
	$(UBUILD)/_ln \
	$(UBUILD)/_ls \
	$(UBUILD)/_mkdir \
	$(UBUILD)/_pingpong \
	$(UBUILD)/_primes \
	$(UBUILD)/_rm \
	$(UBUILD)/_sh \
	$(UBUILD)/_sleep \
	$(UBUILD)/_stressfs \
	$(UBUILD)/_usertests \
	$(UBUILD)/_grind \
	$(UBUILD)/_wc \
	$(UBUILD)/_xargs \
	$(UBUILD)/_zombie \
	$(UBUILD)/_trace \
	$(UBUILD)/_sysinfotest \
	$(UBUILD)/_pgtbltest \
	$(UBUILD)/_bttest \
	$(UBUILD)/_alarmtest \
	$(UBUILD)/_nettests \
	$(UBUILD)/_cowtest \
	$(UBUILD)/_uthread \
	$(UBUILD)/_stats \
	$(UBUILD)/_kalloctest \
	$(UBUILD)/_bcachetest \
	$(UBUILD)/_bigfile \
	$(UBUILD)/_symlinktest \
	$(UBUILD)/_mmaptest

ifeq ($(LAB),lazy)
UPROGS += \
	$(UBUILD)/_lazytests
endif

build: $(KERNEL) $(UPROGS) $(MKFS) $(PH) $(BARRIER)

$(UBUILD)/%.o: $(U)/%.c | $(UBUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(UBUILD)/%.o: $(U)/%.S | $(UBUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(UBUILD)/usys.S: $(U)/usys.pl | $(UBUILD)
	perl $< > $@

$(UBUILD)/usys.o: $(UBUILD)/usys.S | $(UBUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(UBUILD)/_%: $(UBUILD)/%.o $(XV6_ULIB) | $(UBUILD)
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(UBUILD)/$*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(UBUILD)/$*.sym

$(UBUILD)/_forktest: $(UBUILD)/forktest.o $(XV6_ULIB) | $(UBUILD)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(UBUILD)/forktest.o $(UBUILD)/crt0_entry.o $(UBUILD)/crt0.o $(UBUILD)/ustring.o $(UBUILD)/ufile.o $(UBUILD)/ugetpid.o $(UBUILD)/usys.o
	$(OBJDUMP) -S $@ > $(UBUILD)/forktest.asm

$(UBUILD)/_uthread: $(UBUILD)/uthread.o $(UBUILD)/uthread_switch.o $(XV6_ULIB) | $(UBUILD)
	$(LD) $(LDFLAGS) -N -e _start -Ttext 0 -o $@ $(UBUILD)/uthread.o $(UBUILD)/uthread_switch.o $(XV6_ULIB)
	$(OBJDUMP) -S $@ > $(UBUILD)/uthread.asm

$(MKFS): mkfs/mkfs.c $(K)/fs.h $(K)/param.h | $(MKFSBUILD)
	gcc $(XCFLAGS) -Werror -Wall -I. -o $@ $<

$(PH): notxv6/ph.c | $(NOTXV6BUILD)
	gcc -o $@ -g -O2 $(XCFLAGS) $< -pthread

$(BARRIER): notxv6/barrier.c | $(NOTXV6BUILD)
	gcc -o $@ -g -O2 $(XCFLAGS) $< -pthread

ph: $(PH)

barrier: $(BARRIER)

UEXTRA =
UEXTRA += $(U)/xargstest.sh

$(FSIMG): $(MKFS) README README.md $(UEXTRA) $(UPROGS) | $(BUILD)
	$(MKFS) $@ README README.md $(UEXTRA) $(UPROGS)

# Prevent deletion of intermediate files after the first build.
.PRECIOUS: $(KBUILD)/%.o $(UBUILD)/%.o

-include $(KBUILD)/*.d $(UBUILD)/*.d

clean:
	rm -rf $(BUILD) \
		*.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
		.gdbinit fs.img packets.pcap xv6.out* \
		ph barrier \
		$(K)/kernel $(K)/*.o $(K)/*.d $(K)/*.asm $(K)/*.sym \
		$(U)/_* $(U)/*.o $(U)/*.d $(U)/*.asm $(U)/*.sym $(U)/initcode $(U)/initcode.out $(U)/usys.S \
		mkfs/mkfs

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif
ifeq ($(LAB),fs)
CPUS := 1
endif

FWDPORT ?= $(shell expr `id -u` % 5000 + 25999)
NETFWD ?= 0

QEMUOPTS = -machine virt -bios none -kernel $(KERNEL) -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

ifeq ($(NETFWD),1)
QEMUOPTS += -netdev user,id=net0,hostfwd=udp::$(FWDPORT)-:2000 -object filter-dump,id=net0,netdev=net0,file=$(BUILD)/packets.pcap
else
QEMUOPTS += -netdev user,id=net0
endif
QEMUOPTS += -device e1000,netdev=net0,bus=pcie.0
QEMUOPTS += $(QEMUEXTRA)

qemu: $(KERNEL) image
	$(QEMU) $(QEMUOPTS)

qemu-net:
	$(MAKE) qemu NETFWD=1

$(GDBINIT): .gdbinit.tmpl-riscv | $(BUILD)
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $(KERNEL) image $(GDBINIT)
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

qemu-gdb-net:
	$(MAKE) qemu-gdb NETFWD=1

# try to generate a unique port for the echo server
SERVERPORT = $(shell expr `id -u` % 5000 + 25099)

server:
	python3 server.py $(SERVERPORT)

ping:
	python3 ping.py $(FWDPORT)

##
##  FOR testing lab grading script
##

ifneq ($(V),@)
GRADEFLAGS += -v
endif

GRADER_DIR = graders
GRADE_LABS = util syscall net pgtbl traps cow thread lock fs mmap

print-gdbport:
	@echo $(GDBPORT)

grade:
	@echo $(MAKE) clean
	@$(MAKE) clean || \
          (echo "'make clean' failed.  HINT: Do you have another running instance of xv6?" && exit 1)
	$(GRADER_DIR)/grade-lab-$(LAB) $(GRADEFLAGS)

define GRADE_RULE
grade-$(1):
	@echo $$(MAKE) clean
	@$$(MAKE) clean || \
          (echo "'make clean' failed.  HINT: Do you have another running instance of xv6?" && exit 1)
	$$(GRADER_DIR)/grade-lab-$(1) $$(GRADEFLAGS)
endef

$(foreach lab,$(GRADE_LABS),$(eval $(call GRADE_RULE,$(lab))))

smoke-py: $(KERNEL) image
	$(GRADER_DIR)/grade-lab-util --no-make $(GRADEFLAGS)
	$(GRADER_DIR)/grade-lab-syscall --no-make $(GRADEFLAGS)
	$(GRADER_DIR)/grade-lab-net --no-make $(GRADEFLAGS)
	$(GRADER_DIR)/grade-lab-pgtbl --no-make $(GRADEFLAGS)
	$(GRADER_DIR)/grade-lab-traps --no-make $(GRADEFLAGS)
	python3 -c 'import types, gradelib; from gradelib import *; gradelib.options = types.SimpleNamespace(verbose=False, no_make=True, color="never"); r = Runner(); r.run_qemu(shell_script(["symlinktest"]), timeout=30); r.match("^test symlinks: ok$$"); r.match("^test concurrent symlinks: ok$$"); print("fs symlink smoke: OK")'
	python3 -c 'import types, gradelib; from gradelib import *; gradelib.options = types.SimpleNamespace(verbose=False, no_make=True, color="never"); r = Runner(); r.run_qemu(shell_script(["mmaptest"]), timeout=60); r.match("^mmaptest: all tests succeeded$$"); print("mmap smoke: OK")'

test-quick: $(KERNEL) image
	$(XV6TEST) run --suite quick

test-smoke: $(KERNEL) image
	$(XV6TEST) run --suite smoke

smoke: test-smoke

test-smoke-go: test-smoke

test-smoke-util: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags util

test-smoke-syscall: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags syscall

test-smoke-pgtbl: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags pgtbl

test-smoke-traps: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags traps

test-smoke-net: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags net

test-smoke-fs: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags fs

test-smoke-mmap: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags mmap

test-smoke-cow: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags cow

test-smoke-thread: $(KERNEL) image
	$(XV6TEST) run --suite smoke --tags thread

test-thread: $(KERNEL) image $(PH) $(BARRIER)
	$(XV6TEST) run --suite thread

test-cow: $(KERNEL) image
	$(XV6TEST) run --suite cow

test-traps: $(KERNEL) image
	$(XV6TEST) run --suite traps

test-mmap: $(KERNEL) image
	$(XV6TEST) run --suite mmap

test-net: $(KERNEL) image
	$(XV6TEST) run --suite net

test-lock: $(KERNEL) image
	$(XV6TEST) run --suite lock

test-fs: $(KERNEL) image
	$(XV6TEST) run --suite fs

test-usertests: $(KERNEL) image
	$(XV6TEST) run --suite usertests

test-heavy: $(KERNEL) image
	$(XV6TEST) run --suite lock --tags heavy
	$(XV6TEST) run --suite fs --tags heavy
	$(XV6TEST) run --suite usertests --tags heavy

test-all: test-thread test-cow test-traps test-mmap test-net test-lock test-fs test-usertests

regression: test-smoke grade-mmap grade-cow grade-traps

grade-all-heavy: grade-all

grade-all:
	@echo $(MAKE) clean; \
	$(MAKE) clean || \
          (echo "'make clean' failed.  HINT: Do you have another running instance of xv6?" && exit 1); \
	echo $(MAKE) $(KERNEL) image $(GDBINIT); \
	$(MAKE) $(KERNEL) image $(GDBINIT) || \
          (echo "'make $(KERNEL) image $(GDBINIT)' failed." && exit 1); \
	$(GRADER_DIR)/grade-lab-util --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-syscall --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-net --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-pgtbl --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-traps --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-cow --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-thread --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-lock --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-fs --no-make $(GRADEFLAGS) && \
	$(GRADER_DIR)/grade-lab-mmap --no-make $(GRADEFLAGS)

.PHONY: build image clean tags qemu qemu-net qemu-gdb qemu-gdb-net server ping print-gdbport \
	grade $(addprefix grade-,$(GRADE_LABS)) smoke smoke-py test-quick test-smoke test-smoke-go test-smoke-util test-smoke-syscall test-smoke-pgtbl test-smoke-traps test-smoke-net test-smoke-fs test-smoke-mmap test-smoke-cow test-smoke-thread \
	test-thread test-cow test-traps test-mmap test-net test-lock test-fs test-usertests test-heavy test-all regression grade-all grade-all-heavy ph barrier
