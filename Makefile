# Tools
CC      = x86_64-elf-gcc
AS      = nasm
LD      = x86_64-elf-ld

# flags
CFLAGS     = -ffreestanding -O2 -Wall -Wextra -m32 -mgeneral-regs-only
USERCFLAGS = $(CFLAGS) -fno-builtin
ASFLAGS    = -f elf32
LDFLAGS    = -m elf_i386 -T linker.ld

# Automatically pick up every .c and .asm file in src/
SRCS_C   = $(wildcard src/*.c)
SRCS_ASM = $(wildcard src/*.asm)
OBJS     = $(notdir $(SRCS_C:.c=.o)) $(notdir $(SRCS_ASM:.asm=.o))

all: kernel.bin

%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: src/%.asm
	$(AS) $(ASFLAGS) $< -o $@

kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Builds a bootable ISO using the *real* GRUB, not QEMU's -kernel shortcut
iso: kernel.bin
	mkdir -p isodir/boot/grub
	cp kernel.bin isodir/boot/kernel.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o harold.iso isodir

# A blank virtual disk for the ATA driver — created once, then persists across rebuilds
disk.img:
	qemu-img create -f raw disk.img 16M

# The freestanding ring-3 test program + its tiny libc, built separately and not linked into kernel.bin
userspace/test.elf: userspace/test.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/test.c -o userspace/test.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/test.o userspace/libc.o -o userspace/test.elf

# The neural net inference demo — same libc, same fixed load address, different program
userspace/inference.elf: userspace/inference.c userspace/model_data.h userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/inference.c -o userspace/inference.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/inference.o userspace/libc.o -o userspace/inference.elf

# Minimal "cat" — exercises the new open/read/close file I/O syscalls
userspace/cat.elf: userspace/cat.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/cat.c -o userspace/cat.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/cat.o userspace/libc.o -o userspace/cat.elf

# Directory listing — exercises the new LIST syscall
userspace/ls.elf: userspace/ls.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/ls.c -o userspace/ls.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/ls.o userspace/libc.o -o userspace/ls.elf

# Deliberately writes to unmapped memory — proves fault_handler isolates ring 3 crashes instead of halting
userspace/crash.elf: userspace/crash.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/crash.c -o userspace/crash.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/crash.o userspace/libc.o -o userspace/crash.elf

# A real userspace TCP echo server, using nothing but the new socket syscalls — the proof of the sockets milestone
userspace/netecho.elf: userspace/netecho.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/netecho.c -o userspace/netecho.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/netecho.o userspace/libc.o -o userspace/netecho.elf

# Creates saved.txt on disk via the new FAT write syscalls
userspace/save.elf: userspace/save.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/save.c -o userspace/save.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/save.o userspace/libc.o -o userspace/save.elf

# Reads saved.txt back — run after a reboot to prove real persistence, not just an in-memory illusion
userspace/readsaved.elf: userspace/readsaved.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/readsaved.c -o userspace/readsaved.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/readsaved.o userspace/libc.o -o userspace/readsaved.elf

# Appends to saved.txt — proves FAT_OPEN_APPEND resumes from the file's real end
userspace/append.elf: userspace/append.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/append.c -o userspace/append.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/append.o userspace/libc.o -o userspace/append.elf

# Overwrites saved.txt — proves FAT_OPEN_TRUNCATE frees the old cluster chain
userspace/overwrite.elf: userspace/overwrite.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/overwrite.c -o userspace/overwrite.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/overwrite.o userspace/libc.o -o userspace/overwrite.elf

# Writes a secret to raw memory outside its own variables — half of the memory isolation proof
userspace/writer.elf: userspace/writer.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/writer.c -o userspace/writer.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/writer.o userspace/libc.o -o userspace/writer.elf

# Reads that same raw address without writing first — the other half of the isolation proof
userspace/spy.elf: userspace/spy.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/spy.c -o userspace/spy.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/spy.o userspace/libc.o -o userspace/spy.elf

# Deliberately leaks a file descriptor — proves task_reap actually reclaims it, not just memory
userspace/leakfd.elf: userspace/leakfd.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/leakfd.c -o userspace/leakfd.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/leakfd.o userspace/libc.o -o userspace/leakfd.elf

# Exercises libc's malloc/free — proves the free list actually reuses freed space and doesn't
# hand out overlapping memory
userspace/malloctest.elf: userspace/malloctest.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/malloctest.c -o userspace/malloctest.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/malloctest.o userspace/libc.o -o userspace/malloctest.elf

# Run with QEMU's built-in multiboot loader (the shortcut you've been using)
run: kernel.bin disk.img
	qemu-system-x86_64 -kernel kernel.bin -drive file=disk.img,format=raw,if=ide -netdev user,id=n0 -device rtl8139,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=harold_net.pcap

# Run through the real GRUB + real ISO path — the closer proxy for real hardware
run-iso: iso disk.img
	qemu-system-x86_64 -cdrom harold.iso -drive file=disk.img,format=raw,if=ide -netdev user,id=n0 -device rtl8139,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=harold_net.pcap

# Real bidirectional host<->guest networking via macOS's vmnet framework — needs sudo, unlike run/run-iso.
# No fixed subnet requested here on purpose — see the status doc for how to discover what macOS assigns.
run-vmnet: kernel.bin disk.img
	sudo qemu-system-x86_64 -kernel kernel.bin -drive file=disk.img,format=raw,if=ide -netdev vmnet-host,id=n0 -device rtl8139,netdev=n0 -object filter-dump,id=f0,netdev=n0,file=harold_net.pcap

clean:
	rm -f *.o kernel.bin harold.iso userspace/test.o userspace/inference.o userspace/cat.o userspace/ls.o userspace/crash.o userspace/netecho.o userspace/save.o userspace/readsaved.o userspace/append.o userspace/overwrite.o userspace/writer.o userspace/spy.o userspace/leakfd.o userspace/malloctest.o userspace/libc.o userspace/test.elf userspace/inference.elf userspace/cat.elf userspace/ls.elf userspace/crash.elf userspace/netecho.elf userspace/save.elf userspace/readsaved.elf userspace/append.elf userspace/overwrite.elf userspace/writer.elf userspace/spy.elf userspace/leakfd.elf userspace/malloctest.elf
	rm -rf isodir

.PHONY: all run run-iso iso clean