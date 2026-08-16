// Public interface for the minimal DNS resolver

#ifndef DNS_H
#define DNS_H

#include <stdint.h>

// Sets the DNS server to query — call before dns_resolve. A real DNS server is essentially always
// off-subnet, which is exactly what ip_resolve_route's gateway-aware routing exists to reach.
void dns_set_server(const uint8_t server_ip[4]);

// Resolves hostname to an IPv4 address via a single A-record query; returns 0 and fills out_ip, or
// -1 on failure (no server configured, no route, timeout, or no A record in the response)
int dns_resolve(const char *hostname, uint8_t out_ip[4]);

#endif
