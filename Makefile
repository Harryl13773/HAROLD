# Tools
CC      = x86_64-elf-gcc
AS      = nasm
LD      = x86_64-elf-ld

# flags
CFLAGS  = -ffreestanding -O2 -Wall -Wextra -m32 -mgeneral-regs-only
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld

# files
SRCS_C   = $(wildcard src/*.c)
SRCS_ASM = $(wildcard src/*.asm)
OBJS     = $(notdir $(SRCS_C:.c=.o)) $(notdir $(SRCS_ASM:.asm=.o))

# Default target
all: kernel.bin

# One generic rule handles every .c file, one handles every .asm file
%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: src/%.asm
	$(AS) $(ASFLAGS) $< -o $@

# Link everything
kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Run with QEMU
run: kernel.bin
	qemu-system-x86_64 -kernel kernel.bin

# Remove generated files
clean:
	rm -f *.o kernel.bin

.PHONY: all run clean