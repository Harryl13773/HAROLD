// Public interface for the UDP layer.

#ifndef UDP_H
#define UDP_H

#include <stdint.h>

// Sends a UDP datagram wrapped in IP and Ethernet headers, with a correctly computed pseudo-header checksum
void udp_send(uint16_t source_port, uint16_t dest_port, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len);

// Handles a received UDP datagram — currently just the port 7 echo service, for testing
void udp_receive(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *udp_data, uint16_t udp_len);

// Arms a one-shot capture for the next datagram arriving on local_port, so a synchronous
// request/reply protocol (like DNS) can correlate its own response while net_task keeps draining
// the NIC concurrently in the background — unlike arp_resolve's raw NIC poll, which only stays
// safe because it currently never runs after net_task exists.
void udp_arm_response_capture(uint16_t local_port);

// Blocks (via hlt) until the armed capture receives a datagram or timeout_ticks elapses; returns
// the payload length (>= 0) copied into buf and fills out_source_ip, or -1 on timeout. Disarms the
// capture either way.
int udp_wait_for_response(uint8_t *buf, uint32_t buf_size, uint8_t out_source_ip[4], uint32_t timeout_ticks);

#endif