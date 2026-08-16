// Public interface for the IPv4 layer: addressing, checksums, send, and receive dispatch

#ifndef IP_H
#define IP_H

#include <stdint.h>

// Sets our own IPv4 address — call once at boot before anything tries to send or receive IP
void ip_set_address(const uint8_t ip[4]);

// Copies our own IPv4 address into out_ip
void ip_get_address(uint8_t out_ip[4]);

// Sets our subnet mask — defaults to 255.255.255.0 (/24) until called
void ip_set_netmask(const uint8_t mask[4]);

// Sets the default gateway, and marks it configured — call before anything needs to reach an
// off-subnet address (e.g. dns_resolve)
void ip_set_gateway(const uint8_t gateway[4]);

// Resolves the MAC address to actually send a frame to for dest_ip: ARPs dest_ip directly if it's
// on our own subnet, or the configured gateway otherwise (dest_ip itself still goes in the IP
// header — the gateway is only the next hop). Returns 1 and fills out_mac, or 0 if resolution
// failed or dest_ip is off-subnet with no gateway configured.
int ip_resolve_route(const uint8_t dest_ip[4], uint8_t out_mac[6]);

// Computes the standard Internet one's-complement checksum over a buffer
uint16_t ip_checksum(const uint8_t *data, int len);

// Wraps payload in an IP header (given protocol) and an Ethernet header, then sends it
void ip_send(uint8_t protocol, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len);

// Parses a received Ethernet+IP frame and dispatches it if it's addressed to us
void ip_receive(const uint8_t *frame, int frame_len);

#endif