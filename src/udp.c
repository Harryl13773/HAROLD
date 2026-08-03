// UDP layer: datagram send/receive over IP and a port-7 echo service

#include <stdint.h>
#include "terminal.h"
#include "ip.h"
#include "udp.h"

#define UDP_HEADER_LEN 8
#define PSEUDO_HEADER_LEN 12
#define UDP_ECHO_PORT 7 // RFC 862 — a real, standard protocol, not a made-up test port

// Sends a UDP datagram wrapped in IP and Ethernet headers, with a correctly computed pseudo-header checksum
void udp_send(uint16_t source_port, uint16_t dest_port, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len)
{
    // Layout: [12-byte pseudo-header][8-byte UDP header][payload] — the pseudo-header is checksum-only, never sent
    uint8_t buf[PSEUDO_HEADER_LEN + UDP_HEADER_LEN + 1472];
    uint8_t *udp_hdr = buf + PSEUDO_HEADER_LEN;
    uint8_t *udp_payload = udp_hdr + UDP_HEADER_LEN;

    uint16_t max_payload = sizeof(buf) - PSEUDO_HEADER_LEN - UDP_HEADER_LEN;
    if (payload_len > max_payload) // defensive clamp — shouldn't trigger for a normal echo
    {
        payload_len = max_payload;
    }
    uint16_t udp_len = UDP_HEADER_LEN + payload_len;

    uint8_t our_ip[4];
    ip_get_address(our_ip);

    for (int i = 0; i < 4; i++)
    {
        buf[i] = our_ip[i];
    }
    for (int i = 0; i < 4; i++)
    {
        buf[4 + i] = dest_ip[i];
    }
    buf[8] = 0x00; // pseudo-header's zero byte
    buf[9] = 17;   // protocol number for UDP
    buf[10] = (uint8_t)(udp_len >> 8);
    buf[11] = (uint8_t)(udp_len & 0xFF);

    udp_hdr[0] = (uint8_t)(source_port >> 8);
    udp_hdr[1] = (uint8_t)(source_port & 0xFF);
    udp_hdr[2] = (uint8_t)(dest_port >> 8);
    udp_hdr[3] = (uint8_t)(dest_port & 0xFF);
    udp_hdr[4] = (uint8_t)(udp_len >> 8);
    udp_hdr[5] = (uint8_t)(udp_len & 0xFF);
    udp_hdr[6] = 0x00;
    udp_hdr[7] = 0x00; // checksum placeholder, filled in below

    for (uint16_t i = 0; i < payload_len; i++)
    {
        udp_payload[i] = payload[i];
    }

    uint16_t csum = ip_checksum(buf, PSEUDO_HEADER_LEN + udp_len);
    if (csum == 0) // an all-zero checksum means "none computed" per RFC 768 — 0xFFFF is the real value in that case
    {
        csum = 0xFFFF;
    }
    udp_hdr[6] = (uint8_t)(csum >> 8);
    udp_hdr[7] = (uint8_t)(csum & 0xFF);

    ip_send(17, dest_ip, dest_mac, udp_hdr, udp_len);
}

// Handles a received UDP datagram — currently just the port 7 echo service, for testing
void udp_receive(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *udp_data, uint16_t udp_len)
{
    if (udp_len < UDP_HEADER_LEN)
    {
        return;
    }

    uint16_t source_port = ((uint16_t)udp_data[0] << 8) | udp_data[1];
    uint16_t dest_port = ((uint16_t)udp_data[2] << 8) | udp_data[3];

    const uint8_t *payload = udp_data + UDP_HEADER_LEN;
    uint16_t payload_len = udp_len - UDP_HEADER_LEN;

    if (dest_port == UDP_ECHO_PORT)
    {
        udp_send(UDP_ECHO_PORT, source_port, source_ip, source_mac, payload, payload_len);
        terminal_writestring("UDP: echo reply sent\n");
    }
}