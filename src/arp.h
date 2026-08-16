// Public interface for ARP resolution, cache lookups, and request handling.

#ifndef ARP_H
#define ARP_H

#include <stdint.h>

// Resolves target_ip to a MAC (via cache or a request/reply poll); returns 1 and fills out_mac, or 0 on timeout
int arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6]);

// Checks a received frame; if it's a request asking about our_ip, sends a reply. Returns 1 if handled.
int arp_respond_if_request(const uint8_t *frame, int frame_len, const uint8_t our_ip[4]);

// Looks up a cached IP->MAC mapping without sending any network traffic. Returns 1 and fills out_mac if cached.
int arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6]);

#endif