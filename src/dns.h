// Public interface for the minimal DNS resolver.

#ifndef DNS_H
#define DNS_H

#include <stdint.h>

// Sets the DNS server used by dns_resolve
void dns_set_server(const uint8_t server_ip[4]);

// Resolves a hostname to IPv4; returns 0 on success or -1 on failure
int dns_resolve(const char *hostname, uint8_t out_ip[4]);

#endif
