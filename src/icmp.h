// Public interface for handling received ICMP messages

#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

// Handles a received ICMP message; replies to echo requests, ignores everything else
void icmp_handle(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *icmp_data, uint16_t icmp_len);

#endif