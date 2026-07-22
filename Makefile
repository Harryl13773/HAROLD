# Tools
CC      = x86_64-elf-gcc
AS      = nasm
LD      = x86_64-elf-ld

# flags
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -m32
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

# files
OBJS = boot.o kernel.o gdt.o gdt_asm.o

# Default target
all: kernel.bin

# Assemble boot code
boot.o: src/boot.asm
	$(AS) $(ASFLAGS) $< -o $@

# Compile kernel
kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link everything
kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

gdt.o: src/gdt.c
	$(CC) $(CFLAGS) -c $< -o $@

gdt_asm.o: src/gdt_asm.asm
	$(AS) $(ASFLAGS) $< -o $@

# Run with QEMU
run: kernel.bin
	qemu-system-x86_64 -kernel kernel.bin

# Remove generated files
clean:
	rm -f *.o kernel.bin