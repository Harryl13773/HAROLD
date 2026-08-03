// Public interface for the UDP layer

#ifndef UDP_H
#define UDP_H

#include <stdint.h>

// Sends a UDP datagram wrapped in IP and Ethernet headers, with a correctly computed pseudo-header checksum
void udp_send(uint16_t source_port, uint16_t dest_port, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len);

// Handles a received UDP datagram — currently just the port 7 echo service, for testing
void udp_receive(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *udp_data, uint16_t udp_len);

#endif