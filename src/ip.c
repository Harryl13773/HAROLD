#include <stdint.h>
#include "terminal.h"
#include "rtl8139.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "ip.h"

#define ETH_HEADER_LEN 14
#define IP_HEADER_LEN 20 // no options — every packet we build or expect to parse is a plain 20-byte header

static uint8_t our_ip[4] = {0, 0, 0, 0};

// Sets our own IPv4 address — call once at boot before anything tries to send or receive IP
void ip_set_address(const uint8_t ip[4])
{
    for (int i = 0; i < 4; i++)
    {
        our_ip[i] = ip[i];
    }
}

// Copies our own IPv4 address into out_ip
void ip_get_address(uint8_t out_ip[4])
{
    for (int i = 0; i < 4; i++)
    {
        out_ip[i] = our_ip[i];
    }
}

// Computes the standard Internet one's-complement checksum over a buffer
uint16_t ip_checksum(const uint8_t *data, int len)
{
    uint32_t sum = 0;

    for (int i = 0; i + 1 < len; i += 2)
    {
        uint16_t word = ((uint16_t)data[i] << 8) | data[i + 1];
        sum += word;
    }

    if (len & 1) // one leftover byte — treated as the high byte of a final word
    {
        sum += ((uint16_t)data[len - 1] << 8);
    }

    while (sum >> 16) // fold any carry back into the low 16 bits
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

// Returns 1 if the two 4-byte IPv4 addresses match
static int ip_addresses_match(const uint8_t a[4], const uint8_t b[4])
{
    for (int i = 0; i < 4; i++)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
    }
    return 1;
}

// Wraps payload in an IP header (given protocol) and an Ethernet header, then sends it
void ip_send(uint8_t protocol, const uint8_t dest_ip[4], const uint8_t dest_mac[6], const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + 1472]; // 1472 comfortably covers a normal ping payload

    uint16_t max_payload = sizeof(frame) - ETH_HEADER_LEN - IP_HEADER_LEN;
    if (payload_len > max_payload) // defensive clamp — shouldn't trigger for a normal echo reply
    {
        payload_len = max_payload;
    }

    uint8_t our_mac[6];
    rtl8139_get_mac(our_mac);

    for (int i = 0; i < 6; i++)
    {
        frame[i] = dest_mac[i];
    }
    for (int i = 0; i < 6; i++)
    {
        frame[6 + i] = our_mac[i];
    }
    frame[12] = 0x08;
    frame[13] = 0x00; // EtherType IPv4

    uint8_t *ip_hdr = frame + ETH_HEADER_LEN;
    uint16_t total_len = IP_HEADER_LEN + payload_len;

    ip_hdr[0] = 0x45; // version 4, header length 5 dwords (20 bytes, no options)
    ip_hdr[1] = 0x00; // DSCP/ECN unused
    ip_hdr[2] = (uint8_t)(total_len >> 8);
    ip_hdr[3] = (uint8_t)(total_len & 0xFF);
    ip_hdr[4] = 0x00;
    ip_hdr[5] = 0x00; // identification — unused, we never fragment
    ip_hdr[6] = 0x00;
    ip_hdr[7] = 0x00; // flags + fragment offset — none
    ip_hdr[8] = 64;   // TTL
    ip_hdr[9] = protocol;
    ip_hdr[10] = 0x00;
    ip_hdr[11] = 0x00; // checksum placeholder, filled in below
    for (int i = 0; i < 4; i++)
    {
        ip_hdr[12 + i] = our_ip[i];
    }
    for (int i = 0; i < 4; i++)
    {
        ip_hdr[16 + i] = dest_ip[i];
    }

    uint16_t csum = ip_checksum(ip_hdr, IP_HEADER_LEN);
    ip_hdr[10] = (uint8_t)(csum >> 8);
    ip_hdr[11] = (uint8_t)(csum & 0xFF);

    for (uint16_t i = 0; i < payload_len; i++)
    {
        frame[ETH_HEADER_LEN + IP_HEADER_LEN + i] = payload[i];
    }

    rtl8139_send_frame(frame, ETH_HEADER_LEN + IP_HEADER_LEN + payload_len);
}

// Parses a received Ethernet+IP frame and dispatches it if it's addressed to us
void ip_receive(const uint8_t *frame, int frame_len)
{
    if (frame_len < ETH_HEADER_LEN + IP_HEADER_LEN)
    {
        return; // too short to hold even a minimal IP header
    }

    const uint8_t *ip_hdr = frame + ETH_HEADER_LEN;

    if ((ip_hdr[0] >> 4) != 4) // top nibble is the IP version
    {
        return;
    }

    int ihl_bytes = (ip_hdr[0] & 0x0F) * 4; // respect a real header length rather than assuming 20

    uint16_t ip_total_length = ((uint16_t)ip_hdr[2] << 8) | ip_hdr[3];    // the packet's true size, per the IP header itself
    int frame_payload_available = frame_len - ETH_HEADER_LEN - ihl_bytes; // what actually arrived, may include Ethernet padding
    int declared_payload_len = (int)ip_total_length - ihl_bytes;          // what the IP header says is real — this is the trustworthy one

    int payload_len = declared_payload_len;
    if (payload_len > frame_payload_available) // never trust a declared length past what was actually delivered
    {
        payload_len = frame_payload_available;
    }
    if (payload_len < 0)
    {
        payload_len = 0;
    }

    uint8_t dest_ip[4];
    for (int i = 0; i < 4; i++)
    {
        dest_ip[i] = ip_hdr[16 + i];
    }

    if (!ip_addresses_match(dest_ip, our_ip))
    {
        return; // not addressed to us
    }

    uint8_t protocol = ip_hdr[9];
    const uint8_t *source_ip = ip_hdr + 12;
    const uint8_t *source_mac = frame + 6; // Ethernet source — where to reply at the link layer

    if (protocol == 1) // ICMP
    {
        const uint8_t *icmp_data = frame + ETH_HEADER_LEN + ihl_bytes;
        if (payload_len > 0)
        {
            icmp_handle(source_ip, source_mac, icmp_data, (uint16_t)payload_len);
        }
    }
    else if (protocol == 17) // UDP
    {
        const uint8_t *udp_data = frame + ETH_HEADER_LEN + ihl_bytes;
        if (payload_len > 0)
        {
            udp_receive(source_ip, source_mac, udp_data, (uint16_t)payload_len);
        }
    }
    else if (protocol == 6) // TCP
    {
        const uint8_t *tcp_data = frame + ETH_HEADER_LEN + ihl_bytes;
        if (payload_len > 0)
        {
            tcp_receive(source_ip, source_mac, tcp_data, (uint16_t)payload_len);
        }
    }
}