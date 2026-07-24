# Tools
CC      = x86_64-elf-gcc
AS      = nasm
LD      = x86_64-elf-ld

# flags
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -m32 -mgeneral-regs-only
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

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

iso: kernel.bin
	mkdir -p isodir/boot/grub
	cp kernel.bin isodir/boot/kernel.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o harold.iso isodir

disk.img:
	qemu-img create -f raw disk.img 16M

run: kernel.bin disk.img
	qemu-system-x86_64 -kernel kernel.bin -drive file=disk.img,format=raw,if=ide

run-iso: iso disk.img
	qemu-system-x86_64 -cdrom harold.iso -drive file=disk.img,format=raw,if=ide

clean:
	rm -f *.o kernel.bin harold.iso
	rm -rf isodir

.PHONY: all run run-iso iso clean