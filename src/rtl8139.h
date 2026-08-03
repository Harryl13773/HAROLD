// Public interface for the RTL8139 NIC driver

#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

// Locates the card on PCI, enables it, resets it, and reads its burned-in MAC address
void rtl8139_init(void);

// Returns 1 once rtl8139_init has found and reset a real card, 0 otherwise
int rtl8139_is_present(void);

// Copies the card's 6-byte MAC address into out_mac
void rtl8139_get_mac(uint8_t *out_mac);

// Sends one raw Ethernet frame (header + payload), padding to the minimum size if needed
int rtl8139_send_frame(const uint8_t *frame_data, uint16_t frame_len);

// Copies out the next good packet in the ring, if any; returns its length, or 0 if none is ready yet
int rtl8139_receive_packet(uint8_t *out_buffer, uint16_t max_len);

#endif