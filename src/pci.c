#include <stdint.h>
#include "io.h"
#include "terminal.h"
#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_BUS 256
#define PCI_MAX_SLOT 32
#define PCI_MAX_FUNC 8

// Remembers every device pci_scan finds, so pci_find_device doesn't need to rescan
struct pci_device
{
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
};

#define MAX_PCI_DEVICES 32
static struct pci_device devices[MAX_PCI_DEVICES];
static int device_count = 0;

// Builds the CONFIG_ADDRESS value and reads back the matching CONFIG_DATA dword
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | ((uint32_t)offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Builds the CONFIG_ADDRESS value and writes a dword to the matching CONFIG_DATA register
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | ((uint32_t)offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Writes a 4-digit hex value for pci_scan's output
static void print_hex16(uint16_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex_chars[(value >> 12) & 0xF];
    buf[1] = hex_chars[(value >> 8) & 0xF];
    buf[2] = hex_chars[(value >> 4) & 0xF];
    buf[3] = hex_chars[value & 0xF];
    buf[4] = '\0';
    terminal_writestring(buf);
}

// Writes a 2-digit hex value, for bus/slot/func/class output
static void print_hex8(uint8_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex_chars[(value >> 4) & 0xF];
    buf[1] = hex_chars[value & 0xF];
    buf[2] = '\0';
    terminal_writestring(buf);
}

// Checks one bus/slot/function for a real device, recording and printing it if present
static void pci_check_function(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint32_t id_word = pci_config_read_dword(bus, slot, func, 0x00);
    uint16_t vendor_id = id_word & 0xFFFF;

    if (vendor_id == 0xFFFF) // nothing responds at this address
    {
        return;
    }

    uint16_t device_id = (id_word >> 16) & 0xFFFF;
    uint32_t class_word = pci_config_read_dword(bus, slot, func, 0x08);
    uint8_t class_code = (class_word >> 24) & 0xFF;
    uint8_t subclass = (class_word >> 16) & 0xFF;

    if (device_count < MAX_PCI_DEVICES)
    {
        devices[device_count].bus = bus;
        devices[device_count].slot = slot;
        devices[device_count].func = func;
        devices[device_count].vendor_id = vendor_id;
        devices[device_count].device_id = device_id;
        device_count++;
    }

    terminal_writestring("PCI: ");
    print_hex8(bus);
    terminal_writestring(":");
    print_hex8(slot);
    terminal_writestring(".");
    print_hex8(func);
    terminal_writestring(" vendor=");
    print_hex16(vendor_id);
    terminal_writestring(" device=");
    print_hex16(device_id);
    terminal_writestring(" class=");
    print_hex8(class_code);
    terminal_writestring(" subclass=");
    print_hex8(subclass);
    terminal_writestring("\n");
}

// Returns 1 if a slot's function 0 reports itself as multi-function
static int pci_is_multifunction(uint8_t bus, uint8_t slot)
{
    uint32_t header_word = pci_config_read_dword(bus, slot, 0, 0x0C);
    uint8_t header_type = (header_word >> 16) & 0xFF;
    return (header_type & 0x80) != 0;
}

// Walks every bus/slot/function looking for real devices — flat scan, not bridge topology
void pci_scan(void)
{
    terminal_writestring("PCI: scanning for devices\n");

    for (uint32_t bus = 0; bus < PCI_MAX_BUS; bus++)
    {
        for (uint32_t slot = 0; slot < PCI_MAX_SLOT; slot++)
        {
            uint32_t id_word = pci_config_read_dword((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if ((id_word & 0xFFFF) == 0xFFFF) // nothing at function 0 means nothing at this slot
            {
                continue;
            }

            pci_check_function((uint8_t)bus, (uint8_t)slot, 0);

            if (pci_is_multifunction((uint8_t)bus, (uint8_t)slot))
            {
                for (uint32_t func = 1; func < PCI_MAX_FUNC; func++)
                {
                    pci_check_function((uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                }
            }
        }
    }
}

// Looks up an already-scanned device by vendor/device ID
int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func)
{
    for (int i = 0; i < device_count; i++)
    {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id)
        {
            *out_bus = devices[i].bus;
            *out_slot = devices[i].slot;
            *out_func = devices[i].func;
            return 1;
        }
    }
    return 0;
}