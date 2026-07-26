#ifndef PCI_H
#define PCI_H

#include <stdint.h>

// Walks every PCI bus/slot/function and prints what it finds
void pci_scan(void);

// Looks up an already-scanned device by vendor/device ID
int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func);

// Reads a 32-bit PCI config space register for a given bus/slot/function/offset
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

// Writes a 32-bit PCI config space register for a given bus/slot/function/offset
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

#endif