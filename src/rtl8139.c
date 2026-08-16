// RTL8139 NIC driver: PCI init, MAC readout, ring-buffer packet receive, and descriptor-based frame transmit.

#include <stdint.h>
#include "io.h"
#include "terminal.h"
#include "pci.h"
#include "pit.h"
#include "rtl8139.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL_REG_IDR0 0x00    // MAC address, 6 consecutive bytes starting here
#define RTL_REG_TSD0 0x10    // transmit status, 4 descriptors at +0/+4/+8/+12
#define RTL_REG_TSAD0 0x20   // transmit buffer physical address, same 4 descriptors
#define RTL_REG_RBSTART 0x30 // receive buffer physical address
#define RTL_REG_CR 0x37
#define RTL_REG_CAPR 0x38 // current-address-of-packet-read, for the next milestone's RX parsing
#define RTL_REG_IMR 0x3C
#define RTL_REG_RCR 0x44
#define RTL_REG_CONFIG1 0x52

#define RTL_CR_RST 0x10
#define RTL_CR_RE 0x08 // receiver enable
#define RTL_CR_TE 0x04 // transmitter enable

#define RTL_TSD_OWN 0x2000 // set by the card once it's done reading the TX buffer
#define RTL_TSD_TOK 0x8000 // set by the card if that transmission succeeded

#define RTL_CR_BUFE 0x01 // set when the receive ring has nothing left to read

#define RTL8139_RX_RING_SIZE 8192                                 // the size the card itself wraps writes within
#define RTL8139_RX_BUFFER_SIZE (RTL8139_RX_RING_SIZE + 16 + 1500) // +1500 padding required since WRAP is enabled below
#define RTL8139_MIN_FRAME_SIZE 60                                 // Ethernet's minimum frame size before the (hardware-appended) CRC

// Wall-clock bound via PIT ticks — a raw iteration count doesn't reliably bound real TX time under vmnet-host
#define RTL8139_TX_TIMEOUT_MS 3000
// Iteration backstop for the one call site that runs before interrupts (and PIT ticks) are enabled
#define RTL8139_TX_TIMEOUT_ITER_FALLBACK 50000000

#define RTL8139_TX_DESC_COUNT 4 // TSD0-3 / TSAD0-3, 4 bytes apart each

static uint16_t io_base = 0;
static uint8_t mac[6];
static int present = 0;
static uint8_t rx_buffer[RTL8139_RX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t tx_buffer[1514] __attribute__((aligned(4)));
static uint16_t rx_read_offset = 0; // our cursor into the ring, kept in sync with CAPR
// Rotates across all 4 TX descriptors — descriptor 0 was found to permanently wedge after its second use
static int tx_desc_index = 0;

// Returns 1 once rtl8139_init has found and reset a real card, 0 otherwise
int rtl8139_is_present(void)
{
    return present;
}

// Copies the card's 6-byte MAC address into out_mac
void rtl8139_get_mac(uint8_t *out_mac)
{
    for (int i = 0; i < 6; i++)
    {
        out_mac[i] = mac[i];
    }
}

// Writes a 2-digit hex value, for the MAC/io_base printout
static void print_hex8(uint8_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex_chars[(value >> 4) & 0xF];
    buf[1] = hex_chars[value & 0xF];
    buf[2] = '\0';
    terminal_writestring(buf);
}

// Writes a 4-digit hex value, for the io_base printout
static void print_hex16(uint16_t value)
{
    print_hex8((uint8_t)(value >> 8));
    print_hex8((uint8_t)(value & 0xFF));
}

// Locates the card, enables it, resets it, and reads its burned-in MAC address
void rtl8139_init(void)
{
    uint8_t bus, slot, func;

    if (!pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &bus, &slot, &func))
    {
        terminal_writestring("RTL8139: not found on PCI bus\n");
        return;
    }

    uint32_t bar0 = pci_config_read_dword(bus, slot, func, 0x10);
    io_base = (uint16_t)(bar0 & 0xFFFC); // low bits mark I/O space, not part of the address

    uint32_t command = pci_config_read_dword(bus, slot, func, 0x04);
    command |= 0x0005; // bit0 = I/O space enable, bit2 = bus mastering enable
    pci_config_write_dword(bus, slot, func, 0x04, command);

    outb(io_base + RTL_REG_CONFIG1, 0x00); // wake the device up

    outb(io_base + RTL_REG_CR, RTL_CR_RST);        // begin software reset
    while (inb(io_base + RTL_REG_CR) & RTL_CR_RST) // the card clears this bit itself once reset finishes
    {
    }

    for (int i = 0; i < 6; i++)
    {
        mac[i] = inb(io_base + RTL_REG_IDR0 + i);
    }

    outl(io_base + RTL_REG_RBSTART, (uint32_t)rx_buffer);

    outw(io_base + RTL_REG_IMR, 0x0000); // no IRQ line wired up yet — stay fully polled for now

    outl(io_base + RTL_REG_RCR, 0x0000008F); // accept broadcast+multicast+matched+all, WRAP enabled

    outb(io_base + RTL_REG_CR, RTL_CR_RE | RTL_CR_TE); // receiver and transmitter both live

    present = 1;

    terminal_writestring("RTL8139: io_base=0x");
    print_hex16(io_base);
    terminal_writestring(" MAC=");
    for (int i = 0; i < 6; i++)
    {
        print_hex8(mac[i]);
        if (i < 5)
        {
            terminal_writestring(":");
        }
    }
    terminal_writestring("\n");
}

// Sends one raw Ethernet frame, padding up to the minimum frame size if needed
int rtl8139_send_frame(const uint8_t *frame_data, uint16_t frame_len)
{
    if (!present)
    {
        return 0;
    }

    uint16_t send_len = frame_len;
    if (send_len > sizeof(tx_buffer))
    {
        send_len = sizeof(tx_buffer);
    }

    for (uint16_t i = 0; i < send_len; i++)
    {
        tx_buffer[i] = frame_data[i];
    }

    if (send_len < RTL8139_MIN_FRAME_SIZE) // pad with zeros — real NICs won't send an undersized frame
    {
        for (uint16_t i = send_len; i < RTL8139_MIN_FRAME_SIZE; i++)
        {
            tx_buffer[i] = 0x00;
        }
        send_len = RTL8139_MIN_FRAME_SIZE;
    }

    uint16_t tsad_reg = RTL_REG_TSAD0 + (uint16_t)(tx_desc_index * 4);
    uint16_t tsd_reg = RTL_REG_TSD0 + (uint16_t)(tx_desc_index * 4);
    tx_desc_index = (tx_desc_index + 1) % RTL8139_TX_DESC_COUNT;

    outl(io_base + tsad_reg, (uint32_t)tx_buffer);
    outl(io_base + tsd_reg, send_len); // writing the length is what actually triggers the send

    uint32_t freq = pit_get_frequency();
    if (freq == 0)
    {
        freq = 100; // fallback; pit_init always runs before rtl8139_send_frame is ever called
    }
    uint32_t start_tick = pit_get_ticks();
    uint32_t max_ticks = (RTL8139_TX_TIMEOUT_MS * freq) / 1000;
    uint32_t iter_fallback = RTL8139_TX_TIMEOUT_ITER_FALLBACK;

    while (!(inl(io_base + tsd_reg) & RTL_TSD_OWN))
    {
        if ((int32_t)(pit_get_ticks() - start_tick) >= (int32_t)max_ticks)
        {
            break; // real wall-clock bound exceeded — the normal case, interrupts enabled
        }
        if (iter_fallback == 0)
        {
            break; // ticks never advanced — we're in the pre-interrupt boot call, so cap by iteration instead
        }
        iter_fallback--;
    }

    if (!(inl(io_base + tsd_reg) & RTL_TSD_OWN))
    {
        terminal_writestring("RTL8139: TX timed out\n");
        return 0;
    }

    if (inl(io_base + tsd_reg) & RTL_TSD_TOK)
    {
        terminal_writestring("RTL8139: frame sent OK\n");
        return 1;
    }

    terminal_writestring("RTL8139: TX completed but TOK not set\n");
    return 0;
}

// Copies out the next good packet in the ring, if any; returns its length, or 0 if none is ready
int rtl8139_receive_packet(uint8_t *out_buffer, uint16_t max_len)
{
    if (!present)
    {
        return 0;
    }

    while (!(inb(io_base + RTL_REG_CR) & RTL_CR_BUFE)) // clear means at least one packet is waiting
    {
        uint16_t status = *(uint16_t *)(rx_buffer + rx_read_offset);
        uint16_t size = *(uint16_t *)(rx_buffer + rx_read_offset + 2); // includes the trailing 4-byte CRC

        uint16_t packet_len = 0;
        if (status & 0x01) // ROK — a good packet, not a receive error
        {
            packet_len = size - 4; // drop the CRC — callers only want the real frame
            if (packet_len > max_len)
            {
                packet_len = max_len;
            }
            for (uint16_t i = 0; i < packet_len; i++)
            {
                out_buffer[i] = rx_buffer[rx_read_offset + 4 + i]; // no wraparound here — the +1500 pad guarantees this stays contiguous
            }
        }

        rx_read_offset = (rx_read_offset + size + 4 + 3) & ~3; // skip header+data+CRC, round up to a dword boundary
        if (rx_read_offset > RTL8139_RX_RING_SIZE)             // the card's own write pointer wraps here, so ours must match
        {
            rx_read_offset -= RTL8139_RX_RING_SIZE;
        }

        outw(io_base + RTL_REG_CAPR, rx_read_offset - 16); // the card expects this exact -16 offset — a documented hardware quirk

        if (packet_len > 0)
        {
            return packet_len;
        }
        // a bad (non-ROK) packet was skipped — the ring already advanced, so loop back and check the next entry
    }

    return 0; // ring is genuinely empty right now
}