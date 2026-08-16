// Public interface for the TCP layer and its socket-style accept/recv/send/close calls.

#ifndef TCP_H
#define TCP_H

#include <stdint.h>

// Handles a received TCP segment — handshake, echoing data, and now a clean FIN-based close
void tcp_receive(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *tcp_data, uint16_t tcp_len);

// Checks every connection for a timed-out unacknowledged segment and resends it, up to a retry limit
void tcp_check_retransmits(void);

// Blocks until some connection reaches ESTABLISHED and hasn't already been claimed, then returns its index
int tcp_socket_accept(void);

// Blocks until data is available or the connection closes; returns byte count, or 0 once closed and drained
int tcp_socket_recv(int sockfd, uint8_t *buf, uint32_t max_len);

// Sends data on a connection that's still able to send; returns bytes sent, or -1
int tcp_socket_send(int sockfd, const uint8_t *buf, uint32_t len);

// Initiates a clean close by sending our own FIN; returns 0, or -1 if the socket isn't in a closable state
int tcp_socket_close(int sockfd);

#endif