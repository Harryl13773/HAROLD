#ifndef IP_H
#define IP_H

#include <stdint.h>

// Sets our own IPv4 address — call once at boot before anything tries to send or receive IP
void ip_set_address(const uint8_t ip[4]);

// Copies our own IPv4 address into out_ip
void ip_get_address(uint8_t out_ip[4]);

// Computes the standard Internet one's-complement checksum over a buffer
uint16_t ip_checksum(const uint8_t *data, int len);

// Wraps payload in an IP header (given protocol) and an Ethernet header, then sends it
void ip_send(uint8_t protocol, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len);

// Parses a received Ethernet+IP frame and dispatches it if it's addressed to us
void ip_receive(const uint8_t *frame, int frame_len);

#endif