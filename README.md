# HAROLD

A 32-bit x86 operating system built from scratch: its own bootloader hand-off, GDT/IDT/paging, a preemptive round-robin scheduler, a FAT16 filesystem, a hand-rolled TCP/IP stack, and a 19-syscall userspace with 22 ELF programs — no Linux, no borrowed drivers, no libc.

## Features

- **Boot & CPU** — Multiboot handoff from real GRUB, hand-built GDT/IDT, 32 CPU exception handlers with ring-aware fault handling
- **Memory** — bitmap physical frame allocator, per-task page tables, first-fit kernel heap
- **Scheduling** — preemptive round-robin scheduler driven by a 100 Hz PIT tick
- **Drivers** — PS/2 keyboard & mouse, COM1 serial, VGA text terminal, ATA PIO disk (primary/secondary channel fallback), CMOS RTC, PCI enumeration, RTL8139 NIC
- **Filesystem** — real FAT16: subdirectories, long filenames, create/truncate/append/in-place writes
- **Networking** — ARP, IPv4 with gateway routing, ICMP, UDP, TCP (handshake, RFC 6298 adaptive retransmit timeout), and a DNS resolver
- **Userspace** — 19-call syscall ABI, a from-scratch libc, an interactive shell, and 22 programs including a mouse-driven text-mode GUI and a fixed-point (Q16.16) neural-network inference demo that runs with no FPU

## Requirements

- `i386-elf` (or `x86_64-elf`) cross-compiler toolchain (`gcc`, `ld`)
- `nasm`
- `qemu-system-x86_64`
- `i686-elf-grub-mkrescue` (only needed for the `iso`/`run-iso` targets)

## Build & run

```sh
make          # builds kernel.bin and every userspace/*.elf
make run      # boots kernel.bin directly via QEMU's built-in multiboot loader
make run-iso  # builds a real GRUB ISO and boots that instead (closer to real hardware)
make clean    # removes all build output
```

`disk.img` is the FAT16-formatted virtual disk the ATA driver reads/writes; it persists across rebuilds so files written during one run are still there on the next.

Networking defaults to QEMU user-mode NAT. `make run-vmnet` bridges the guest onto your host's real LAN via macOS `vmnet-host` (requires `sudo`) — edit the placeholder `bridge_ip`/`our_ip` addresses in `src/kernel.c` to match your own network before using it.

## Project layout

```
src/         kernel: boot, CPU setup, drivers, memory, scheduler, filesystem, network stack, syscalls
userspace/   libc + 22 ELF programs that run in ring 3
training/    Python scripts that train the small neural net and export its weights as fixed-point C data
```
