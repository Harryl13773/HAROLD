/*
Minimal DNS resolver: encodes a single A-record query, sends it over UDP to a configured
server, and parses the first A record out of the reply — enough to turn a hostname into an
IPv4 address, nothing more (no caching, no AAAA/CNAME following, no retries).
*/

#include <stdint.h>
#include "terminal.h"
#include "ip.h"
#include "udp.h"
#include "dns.h"

#define DNS_PORT 53
#define DNS_QUERY_LOCAL_PORT 53000 // fixed — only one query is ever in flight at a time on this stack
#define DNS_TIMEOUT_TICKS 200      //// Wait up to 2s for a DNS response
#define DNS_MAX_NAME_LEN 253       // RFC 1035's own limit on a full domain name

static uint8_t dns_server_ip[4] = {0, 0, 0, 0};
static int dns_server_configured = 0;

// Sets the DNS server to query call before dns_resolve
void dns_set_server(const uint8_t server_ip[4])
{
    for (int i = 0; i < 4; i++)
    {
        dns_server_ip[i] = server_ip[i];
    }
    dns_server_configured = 1;
}

// Encodes a domain name into DNS label format; returns its length or -1 on error
static int dns_encode_name(const char *hostname, uint8_t *out, int out_size)
{
    int out_pos = 0;
    int label_start = 0;
    int i = 0;

    while (1)
    {
        char c = hostname[i];

        if (c == '.' || c == '\0')
        {
            int label_len = i - label_start;
            if (label_len == 0 || label_len > 63 || out_pos + 1 + label_len >= out_size)
            {
                return -1; // empty label ("a..b"), oversized label, or would overflow the buffer
            }

            out[out_pos++] = (uint8_t)label_len;
            for (int j = 0; j < label_len; j++)
            {
                out[out_pos++] = (uint8_t)hostname[label_start + j];
            }

            label_start = i + 1;
            if (c == '\0')
            {
                break;
            }
        }

        i++;
        if (i > DNS_MAX_NAME_LEN)
        {
            return -1;
        }
    }

    if (out_pos >= out_size)
    {
        return -1;
    }
    out[out_pos++] = 0; // the root label: terminates the name

    return out_pos;
}

// Resolves hostname to an IPv4 address via a single A-record query
int dns_resolve(const char *hostname, uint8_t out_ip[4])
{
    if (!dns_server_configured)
    {
        terminal_writestring("DNS: no server configured\n");
        return -1;
    }

    uint8_t dns_mac[6];
    if (!ip_resolve_route(dns_server_ip, dns_mac)) // almost always off-subnet — what the gateway routing is for
    {
        terminal_writestring("DNS: could not resolve a route to the DNS server\n");
        return -1;
    }

    uint8_t query[512];

    uint16_t id = 0x1234; // fixed — only one query is ever in flight at a time on this stack
    query[0] = (uint8_t)(id >> 8);
    query[1] = (uint8_t)(id & 0xFF);
    query[2] = 0x01; // flags: RD=1 (recursion desired), standard query, everything else 0
    query[3] = 0x00;
    query[4] = 0x00; // QDCOUNT = 1
    query[5] = 0x01;
    query[6] = 0x00; // ANCOUNT = 0
    query[7] = 0x00;
    query[8] = 0x00; // NSCOUNT = 0
    query[9] = 0x00;
    query[10] = 0x00; // ARCOUNT = 0
    query[11] = 0x00;

    int name_len = dns_encode_name(hostname, query + 12, (int)sizeof(query) - 12 - 4);
    if (name_len < 0)
    {
        terminal_writestring("DNS: hostname too long or malformed\n");
        return -1;
    }

    int qpos = 12 + name_len;
    query[qpos++] = 0x00; // QTYPE = A
    query[qpos++] = 0x01;
    query[qpos++] = 0x00; // QCLASS = IN
    query[qpos++] = 0x01;

    udp_arm_response_capture(DNS_QUERY_LOCAL_PORT); // Arm first in case the reply arrives immediately
    udp_send(DNS_QUERY_LOCAL_PORT, DNS_PORT, dns_server_ip, dns_mac, query, (uint16_t)qpos);

    terminal_writestring("DNS: query sent for ");
    terminal_writestring(hostname);
    terminal_writestring("\n");

    uint8_t response[512];
    uint8_t response_source_ip[4];
    int response_len = udp_wait_for_response(response, sizeof(response), response_source_ip, DNS_TIMEOUT_TICKS);
    if (response_len < 0)
    {
        terminal_writestring("DNS: timed out waiting for reply\n");
        return -1;
    }
    if (response_len < 12)
    {
        terminal_writestring("DNS: reply too short to hold a header\n");
        return -1;
    }

    for (int i = 0; i < 4; i++)
    {
        if (response_source_ip[i] != dns_server_ip[i])
        {

            // Only accept replies from our configured DNS server
            terminal_writestring("DNS: reply came from an unexpected source, ignoring\n");
            return -1;
        }
    }

    uint16_t ancount = ((uint16_t)response[6] << 8) | response[7];
    if (ancount == 0)
    {
        terminal_writestring("DNS: no answer records (NXDOMAIN or empty response)\n");
        return -1;
    }

    // Skip the echoed question section to reach the DNS answers
    int pos = 12;
    while (pos < response_len && response[pos] != 0)
    {
        pos += response[pos] + 1;
    }
    pos += 1 + 4; // the root label byte, then QTYPE+QCLASS

    // Find the first A record, skipping other record types
    for (uint16_t a = 0; a < ancount && pos < response_len; a++)
    {

        // Skip NAME, handling either a compression pointer or inline name
        if ((response[pos] & 0xC0) == 0xC0)
        {
            pos += 2;
        }
        else
        {
            while (pos < response_len && response[pos] != 0)
            {
                pos += response[pos] + 1;
            }
            pos += 1;
        }

        if (pos + 10 > response_len) // TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2)
        {
            break;
        }

        uint16_t rtype = ((uint16_t)response[pos] << 8) | response[pos + 1];
        uint16_t rdlength = ((uint16_t)response[pos + 8] << 8) | response[pos + 9];
        pos += 10;

        if (rtype == 1 && rdlength == 4 && pos + 4 <= response_len) // A record, a real 4-byte IPv4 address
        {
            for (int i = 0; i < 4; i++)
            {
                out_ip[i] = response[pos + i];
            }
            terminal_writestring("DNS: resolved\n");
            return 0;
        }

        pos += rdlength;
    }

    terminal_writestring("DNS: no A record found in response\n");
    return -1;
}
