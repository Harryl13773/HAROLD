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

# Deliberately divides by zero — proves fault_handler isolates ring 3 crashes instead of halting
userspace/crash.elf: userspace/crash.c userspace/libc.c userspace/libc.h userspace/linker.ld
	$(CC) $(USERCFLAGS) -c userspace/crash.c -o userspace/crash.o
	$(CC) $(USERCFLAGS) -c userspace/libc.c -o userspace/libc.o
	$(LD) -m elf_i386 -T userspace/linker.ld userspace/crash.o userspace/libc.o -o userspace/crash.elf

# Run with QEMU's built-in multiboot loader (the shortcut you've been using)
run: kernel.bin disk.img
	qemu-system-x86_64 -kernel kernel.bin -drive file=disk.img,format=raw,if=ide

# Run through the real GRUB + real ISO path — the closer proxy for real hardware
run-iso: iso disk.img
	qemu-system-x86_64 -cdrom harold.iso -drive file=disk.img,format=raw,if=ide

clean:
	rm -f *.o kernel.bin harold.iso userspace/test.o userspace/inference.o userspace/cat.o userspace/ls.o userspace/crash.o userspace/libc.o userspace/test.elf userspace/inference.elf userspace/cat.elf userspace/ls.elf userspace/crash.elf
	rm -rf isodir

.PHONY: all run run-iso iso clean