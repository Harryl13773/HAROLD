// Public interface for GDT setup and selector constants.

#ifndef GDT_H
#define GDT_H

// GDT selectors: index * 8, matching gdt_install()'s layout
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE 0x18
#define GDT_USER_DATA 0x20
#define GDT_TSS 0x28

// Builds and loads the null, kernel, user, and TSS descriptors
void gdt_install(void);

#endif