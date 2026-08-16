// ICMP handling: replies to echo requests (ping) over IP.

#include <stdint.h>
#include "terminal.h"
#include "ip.h"
#include "icmp.h"

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_HEADER_LEN 8 // type + code + checksum + identifier + sequence

// Handles a received ICMP message; replies to echo requests, ignores everything else
void icmp_handle(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *icmp_data, uint16_t icmp_len)
{
    if (icmp_len < ICMP_HEADER_LEN)
    {
        return;
    }

    if (icmp_data[0] != ICMP_TYPE_ECHO_REQUEST) // only echo request gets a reply — not a general ICMP stack
    {
        return;
    }

    uint8_t reply[1472];
    if (icmp_len > sizeof(reply))
    {
        icmp_len = sizeof(reply); // defensive clamp — a real ping payload is far smaller than this
    }

    for (uint16_t i = 0; i < icmp_len; i++)
    {
        reply[i] = icmp_data[i]; // copy identifier, sequence, and payload through unchanged — a true echo
    }

    reply[0] = ICMP_TYPE_ECHO_REPLY;
    reply[1] = 0; // code 0
    reply[2] = 0x00;
    reply[3] = 0x00; // checksum placeholder, filled in below

    uint16_t csum = ip_checksum(reply, icmp_len);
    reply[2] = (uint8_t)(csum >> 8);
    reply[3] = (uint8_t)(csum & 0xFF);

    ip_send(1, source_ip, source_mac, reply, icmp_len);

    terminal_writestring("ICMP: echo reply sent\n");
}