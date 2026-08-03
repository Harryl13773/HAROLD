// ARP protocol: IP->MAC resolution cache, request sending, and reply handling

#include <stdint.h>
#include "terminal.h"
#include "rtl8139.h"
#include "arp.h"

#define ETHERTYPE_ARP_HIGH 0x08
#define ETHERTYPE_ARP_LOW 0x06

#define ARP_OP_REQUEST 0x0001
#define ARP_OP_REPLY 0x0002

#define ARP_FRAME_SIZE 42         // 14-byte Ethernet header + 28-byte ARP payload
#define ARP_POLL_ATTEMPTS 2000000 // bounded — no timer available this early in boot, so this is a busy-wait proxy for a timeout
#define ARP_CACHE_SIZE 16

struct arp_cache_entry
{
    uint8_t ip[4];
    uint8_t mac[6];
    int valid;
};

static struct arp_cache_entry arp_cache[ARP_CACHE_SIZE];
static int arp_cache_next_slot = 0; // round-robin eviction once the cache fills

// Returns 1 and fills out_mac if ip is already cached; sends nothing
int arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid)
        {
            continue;
        }
        int match = 1;
        for (int j = 0; j < 4; j++)
        {
            if (arp_cache[i].ip[j] != ip[j])
            {
                match = 0;
            }
        }
        if (match)
        {
            for (int j = 0; j < 6; j++)
            {
                out_mac[j] = arp_cache[i].mac[j];
            }
            return 1;
        }
    }
    return 0;
}

// Records or refreshes a cached IP->MAC mapping
static void arp_cache_insert(const uint8_t ip[4], const uint8_t mac[6])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (!arp_cache[i].valid)
        {
            continue;
        }
        int match = 1;
        for (int j = 0; j < 4; j++)
        {
            if (arp_cache[i].ip[j] != ip[j])
            {
                match = 0;
            }
        }
        if (match) // already cached — refresh in place instead of adding a duplicate
        {
            for (int j = 0; j < 6; j++)
            {
                arp_cache[i].mac[j] = mac[j];
            }
            return;
        }
    }

    int slot = arp_cache_next_slot; // not cached yet — take the next slot, evicting round-robin once full
    arp_cache_next_slot = (arp_cache_next_slot + 1) % ARP_CACHE_SIZE;

    for (int j = 0; j < 4; j++)
    {
        arp_cache[slot].ip[j] = ip[j];
    }
    for (int j = 0; j < 6; j++)
    {
        arp_cache[slot].mac[j] = mac[j];
    }
    arp_cache[slot].valid = 1;
}

// Writes a big-endian 16-bit value into buf at offset
static void put_be16(uint8_t *buf, int offset, uint16_t value)
{
    buf[offset] = (uint8_t)(value >> 8);
    buf[offset + 1] = (uint8_t)(value & 0xFF);
}

// Reads a big-endian 16-bit value from buf at offset
static uint16_t get_be16(const uint8_t *buf, int offset)
{
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}

// Writes a 2-digit hex value, for printing a resolved MAC
static void print_hex8(uint8_t value)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex_chars[(value >> 4) & 0xF];
    buf[1] = hex_chars[value & 0xF];
    buf[2] = '\0';
    terminal_writestring(buf);
}

// Sends an ARP request for target_ip, then polls received frames for a matching reply
int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6])
{
    if (arp_cache_lookup(target_ip, out_mac)) // already know this one — no need to touch the network at all
    {
        terminal_writestring("ARP: cache hit, no request sent\n");
        return 1;
    }

    uint8_t our_mac[6];
    rtl8139_get_mac(our_mac);

    uint8_t frame[ARP_FRAME_SIZE];

    for (int i = 0; i < 6; i++)
    {
        frame[i] = 0xFF; // destination: broadcast — we don't know who has this IP yet
    }
    for (int i = 0; i < 6; i++)
    {
        frame[6 + i] = our_mac[i]; // source: us
    }
    frame[12] = ETHERTYPE_ARP_HIGH;
    frame[13] = ETHERTYPE_ARP_LOW;

    uint8_t *arp = frame + 14;
    put_be16(arp, 0, 0x0001); // hardware type: Ethernet
    put_be16(arp, 2, 0x0800); // protocol type: IPv4
    arp[4] = 6;               // hardware address length
    arp[5] = 4;               // protocol address length
    put_be16(arp, 6, ARP_OP_REQUEST);

    for (int i = 0; i < 6; i++)
    {
        arp[8 + i] = our_mac[i]; // sender hardware address
    }
    for (int i = 0; i < 4; i++)
    {
        arp[14 + i] = 0x00; // sender protocol address — 0.0.0.0, since we have no IP configured yet
    }
    for (int i = 0; i < 6; i++)
    {
        arp[18 + i] = 0x00; // target hardware address — unknown, that's what we're asking for
    }
    for (int i = 0; i < 4; i++)
    {
        arp[24 + i] = target_ip[i]; // target protocol address
    }

    terminal_writestring("ARP: sending request\n");
    rtl8139_send_frame(frame, ARP_FRAME_SIZE);

    uint8_t rx_buf[128];
    for (int attempt = 0; attempt < ARP_POLL_ATTEMPTS; attempt++)
    {
        int len = rtl8139_receive_packet(rx_buf, sizeof(rx_buf));
        if (len < ARP_FRAME_SIZE)
        {
            continue; // either nothing arrived, or too short to hold a full ARP packet
        }

        if (rx_buf[12] != ETHERTYPE_ARP_HIGH || rx_buf[13] != ETHERTYPE_ARP_LOW)
        {
            continue; // not ARP
        }

        uint8_t *reply_arp = rx_buf + 14;
        if (get_be16(reply_arp, 6) != ARP_OP_REPLY)
        {
            continue;
        }

        int ip_matches = 1;
        for (int i = 0; i < 4; i++)
        {
            if (reply_arp[14 + i] != target_ip[i])
            {
                ip_matches = 0;
            }
        }
        if (!ip_matches)
        {
            continue; // an ARP reply, but not answering our question
        }

        for (int i = 0; i < 6; i++)
        {
            out_mac[i] = reply_arp[8 + i]; // sender hardware address — the answer we wanted
        }

        arp_cache_insert(target_ip, out_mac);

        terminal_writestring("ARP: reply received, MAC=");
        for (int i = 0; i < 6; i++)
        {
            print_hex8(out_mac[i]);
            if (i < 5)
            {
                terminal_writestring(":");
            }
        }
        terminal_writestring("\n");
        return 1;
    }

    terminal_writestring("ARP: timed out waiting for reply\n");
    return 0;
}

// Checks a received frame; if it's a request for our_ip, replies with our real MAC — the piece a requester alone can't provide
int arp_respond_if_request(const uint8_t *frame, int frame_len, const uint8_t our_ip[4])
{
    if (frame_len < ARP_FRAME_SIZE)
    {
        return 0;
    }
    if (frame[12] != ETHERTYPE_ARP_HIGH || frame[13] != ETHERTYPE_ARP_LOW)
    {
        return 0;
    }

    const uint8_t *request_arp = frame + 14;
    if (get_be16(request_arp, 6) != ARP_OP_REQUEST)
    {
        return 0;
    }

    int ip_matches = 1;
    for (int i = 0; i < 4; i++)
    {
        if (request_arp[24 + i] != our_ip[i])
        {
            ip_matches = 0;
        }
    }
    if (!ip_matches)
    {
        return 0; // a real request, just not asking about us
    }

    uint8_t our_mac[6];
    rtl8139_get_mac(our_mac);

    uint8_t requester_mac[6];
    for (int i = 0; i < 6; i++)
    {
        requester_mac[i] = request_arp[8 + i];
    }
    uint8_t requester_ip[4];
    for (int i = 0; i < 4; i++)
    {
        requester_ip[i] = request_arp[14 + i];
    }

    arp_cache_insert(requester_ip, requester_mac); // they told us their mapping for free — no reason not to remember it

    uint8_t reply[ARP_FRAME_SIZE];
    for (int i = 0; i < 6; i++)
    {
        reply[i] = requester_mac[i]; // destination: straight back to whoever asked
    }
    for (int i = 0; i < 6; i++)
    {
        reply[6 + i] = our_mac[i];
    }
    reply[12] = ETHERTYPE_ARP_HIGH;
    reply[13] = ETHERTYPE_ARP_LOW;

    uint8_t *reply_arp = reply + 14;
    put_be16(reply_arp, 0, 0x0001);
    put_be16(reply_arp, 2, 0x0800);
    reply_arp[4] = 6;
    reply_arp[5] = 4;
    put_be16(reply_arp, 6, ARP_OP_REPLY);
    for (int i = 0; i < 6; i++)
    {
        reply_arp[8 + i] = our_mac[i]; // sender hardware address — the answer being given
    }
    for (int i = 0; i < 4; i++)
    {
        reply_arp[14 + i] = our_ip[i];
    }
    for (int i = 0; i < 6; i++)
    {
        reply_arp[18 + i] = requester_mac[i];
    }
    for (int i = 0; i < 4; i++)
    {
        reply_arp[24 + i] = requester_ip[i];
    }

    if (rtl8139_send_frame(reply, ARP_FRAME_SIZE))
    {
        terminal_writestring("ARP: replied to a request for our IP\n");
    }
    else
    {
        terminal_writestring("ARP: recognized a request for our IP, but the reply failed to send\n");
    }
    return 1;
}