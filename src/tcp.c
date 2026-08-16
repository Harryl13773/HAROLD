// Minimal TCP: per-connection state machine (SYN/handshake, data, FIN close), retransmit timer, and socket calls.

#include <stdint.h>
#include "terminal.h"
#include "ip.h"
#include "pit.h"
#include "tcp.h"

#define TCP_HEADER_LEN 20 // no options — every segment we build or expect to parse is a plain 20-byte header
#define PSEUDO_HEADER_LEN 12
#define TCP_TEST_PORT 7 // RFC 862 defines echo on port 7 for both UDP and TCP — matches the UDP milestone
#define MAX_TCP_CONNECTIONS 8

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_RTO_INITIAL_TICKS 50 // 500ms — used only before any real RTT sample exists
#define TCP_RTO_MIN_TICKS 2      // 20ms
#define TCP_RTO_MAX_TICKS 200    // 2s — keeps one bad RTTVAR spike from letting RTO grow unbounded
#define TCP_MAX_RETRANSMITS 5

enum tcp_state
{
    TCP_CLOSED = 0, // also doubles as "slot unused" for the connection table
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT, // their FIN arrived and we ACKed it, but we might still have data left to send
    TCP_LAST_ACK    // we've sent our own FIN (actively or in response to CLOSE_WAIT), waiting for the final ACK
};

struct tcp_connection
{
    uint8_t remote_ip[4];
    uint8_t remote_mac[6];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t our_seq;   // next sequence number we will send
    uint32_t their_seq; // next sequence number we expect from them
    enum tcp_state state;

    // Incoming data waiting for a userspace socket_recv() call — the kernel no longer echoes automatically
    uint8_t rx_buffer[512];
    uint16_t rx_head;
    uint16_t rx_tail;
    int rx_closed; // set once their FIN arrives — signals EOF to a blocked recv() once the buffer drains
    int accepted;  // set once a socket_accept() call has claimed this connection, so it's only handed out once

    // The most recently sent segment that might still need retransmitting — only one is ever in flight at a time
    uint8_t pending_data[1460];
    uint16_t pending_len;
    uint32_t pending_seq;
    uint8_t pending_flags;
    int pending; // 0 = nothing outstanding, 1 = waiting on an ACK
    uint32_t last_send_tick;
    int retransmit_count;
    uint32_t current_rto;
    uint32_t srtt;
    uint32_t rttvar;
    uint32_t rto; // derived from srtt/rttvar — the connection's current best RTO estimate
    int rtt_valid;
};

static struct tcp_connection connections[MAX_TCP_CONNECTIONS];

// Reads a big-endian 16-bit value from buf at offset
static uint16_t get_be16(const uint8_t *buf, int offset)
{
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}

// Writes a big-endian 16-bit value into buf at offset
static void put_be16(uint8_t *buf, int offset, uint16_t value)
{
    buf[offset] = (uint8_t)(value >> 8);
    buf[offset + 1] = (uint8_t)(value & 0xFF);
}

// Reads a big-endian 32-bit value from buf at offset
static uint32_t get_be32(const uint8_t *buf, int offset)
{
    return ((uint32_t)buf[offset] << 24) | ((uint32_t)buf[offset + 1] << 16) | ((uint32_t)buf[offset + 2] << 8) | buf[offset + 3];
}

// Writes a big-endian 32-bit value into buf at offset
static void put_be32(uint8_t *buf, int offset, uint32_t value)
{
    buf[offset] = (uint8_t)(value >> 24);
    buf[offset + 1] = (uint8_t)(value >> 16);
    buf[offset + 2] = (uint8_t)(value >> 8);
    buf[offset + 3] = (uint8_t)(value & 0xFF);
}

// Finds an existing connection matching this remote IP+port, if any
static struct tcp_connection *tcp_find_connection(const uint8_t remote_ip[4], uint16_t remote_port)
{
    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
    {
        if (connections[i].state == TCP_CLOSED)
        {
            continue;
        }
        if (connections[i].remote_port != remote_port)
        {
            continue;
        }
        int ip_matches = 1;
        for (int j = 0; j < 4; j++)
        {
            if (connections[i].remote_ip[j] != remote_ip[j])
            {
                ip_matches = 0;
            }
        }
        if (ip_matches)
        {
            return &connections[i];
        }
    }
    return 0;
}

// Finds a free slot for a new connection, or NULL if the table is full
static struct tcp_connection *tcp_alloc_connection(void)
{
    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
    {
        if (connections[i].state == TCP_CLOSED)
        {
            return &connections[i];
        }
    }
    return 0;
}

// Builds and sends one TCP segment for a connection, with a correctly computed pseudo-header checksum
static void tcp_send_segment(struct tcp_connection *conn, uint8_t flags, uint32_t seq, uint32_t ack, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[PSEUDO_HEADER_LEN + TCP_HEADER_LEN + 1460];
    uint8_t *tcp_hdr = buf + PSEUDO_HEADER_LEN;
    uint8_t *tcp_payload = tcp_hdr + TCP_HEADER_LEN;

    uint16_t max_payload = sizeof(buf) - PSEUDO_HEADER_LEN - TCP_HEADER_LEN;
    if (payload_len > max_payload) // defensive clamp — not reachable yet since nothing calls this with real data
    {
        payload_len = max_payload;
    }
    uint16_t tcp_len = TCP_HEADER_LEN + payload_len;

    uint8_t our_ip[4];
    ip_get_address(our_ip);

    for (int i = 0; i < 4; i++)
    {
        buf[i] = our_ip[i];
    }
    for (int i = 0; i < 4; i++)
    {
        buf[4 + i] = conn->remote_ip[i];
    }
    buf[8] = 0x00;
    buf[9] = 6; // protocol number for TCP
    buf[10] = (uint8_t)(tcp_len >> 8);
    buf[11] = (uint8_t)(tcp_len & 0xFF);

    put_be16(tcp_hdr, 0, conn->local_port);
    put_be16(tcp_hdr, 2, conn->remote_port);
    put_be32(tcp_hdr, 4, seq);
    put_be32(tcp_hdr, 8, ack);
    tcp_hdr[12] = 5 << 4; // data offset: 5 dwords = 20 bytes, no options
    tcp_hdr[13] = flags;
    put_be16(tcp_hdr, 14, 4096); // window — a placeholder until real flow control exists
    put_be16(tcp_hdr, 16, 0);    // checksum placeholder, filled in below
    put_be16(tcp_hdr, 18, 0);    // urgent pointer — unused

    for (uint16_t i = 0; i < payload_len; i++)
    {
        tcp_payload[i] = payload[i];
    }

    uint16_t csum = ip_checksum(buf, PSEUDO_HEADER_LEN + tcp_len);
    put_be16(tcp_hdr, 16, csum); // unlike UDP, a TCP checksum of exactly 0 is valid as-is — no 0xFFFF substitution

    ip_send(6, conn->remote_ip, conn->remote_mac, tcp_hdr, tcp_len);
}

// Updates the RTO from the pending segment's RTT sample, then clears it
static void tcp_rtt_sample(struct tcp_connection *conn)
{
    if (conn->retransmit_count == 0) // Karn's algorithm: ignore RTT samples from retransmitted segments
    {
        uint32_t measured = pit_get_ticks() - conn->last_send_tick;
        if (measured == 0)
        {
            measured = 1; // Clamp same-tick RTTs to one PIT tick to avoid an unrealistically low RTO
        }

        if (!conn->rtt_valid)
        {
            conn->srtt = measured << 3;   // srtt stored as actual_srtt * 8
            conn->rttvar = measured << 1; // rttvar stored as actual_rttvar * 4, i.e. (R/2) * 4
            conn->rtt_valid = 1;
        }
        else
        {
            int32_t delta = (int32_t)measured - (int32_t)(conn->srtt >> 3);
            conn->srtt = (uint32_t)((int32_t)conn->srtt + delta); // srtt += (R - srtt/8)
            if (delta < 0)
            {
                delta = -delta;
            }
            conn->rttvar = (uint32_t)((int32_t)conn->rttvar + (delta - (int32_t)(conn->rttvar >> 2)));
        }

        // rttvar is stored scaled by 4, so this directly computes SRTT + 4*RTTVAR
        conn->rto = (conn->srtt >> 3) + conn->rttvar;
        if (conn->rto < TCP_RTO_MIN_TICKS)
        {
            conn->rto = TCP_RTO_MIN_TICKS;
        }
        if (conn->rto > TCP_RTO_MAX_TICKS)
        {
            conn->rto = TCP_RTO_MAX_TICKS;
        }

        terminal_writestring("TCP: RTT sample=");
        terminal_print_dec(measured);
        terminal_writestring(" ticks, srtt=");
        terminal_print_dec(conn->srtt >> 3);
        terminal_writestring(" rto=");
        terminal_print_dec(conn->rto);
        terminal_writestring(" ticks\n");
    }

    conn->pending = 0;
}

// Sends a segment that expects an ACK, and records it so tcp_check_retransmits can resend it if needed
static void tcp_send_tracked(struct tcp_connection *conn, uint8_t flags, uint32_t seq, const uint8_t *payload, uint16_t payload_len)
{
    tcp_send_segment(conn, flags, seq, conn->their_seq, payload, payload_len);

    uint16_t store_len = payload_len;
    if (store_len > sizeof(conn->pending_data)) // shouldn't happen — tcp_send_segment already clamps to the same ceiling
    {
        store_len = sizeof(conn->pending_data);
    }
    for (uint16_t i = 0; i < store_len; i++)
    {
        conn->pending_data[i] = payload[i];
    }
    conn->pending_len = store_len;
    conn->pending_seq = seq;
    conn->pending_flags = flags;
    conn->pending = 1;
    conn->last_send_tick = pit_get_ticks();
    conn->retransmit_count = 0;
    conn->current_rto = conn->rtt_valid ? conn->rto : TCP_RTO_INITIAL_TICKS;
}

// Checks every connection for a timed-out unacknowledged segment and resends it, up to a retry limit
void tcp_check_retransmits(void)
{
    uint32_t now = pit_get_ticks();

    for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
    {
        struct tcp_connection *conn = &connections[i];

        if (conn->state == TCP_CLOSED || !conn->pending)
        {
            continue;
        }
        if (now - conn->last_send_tick < conn->current_rto)
        {
            continue; // not due yet
        }

        if (conn->retransmit_count >= TCP_MAX_RETRANSMITS)
        {
            conn->state = TCP_CLOSED; // giving up — a real stack would also send RST here
            conn->pending = 0;
            terminal_writestring("TCP: gave up after repeated retransmit failures, connection aborted\n");
            continue;
        }

        tcp_send_segment(conn, conn->pending_flags, conn->pending_seq, conn->their_seq, conn->pending_len > 0 ? conn->pending_data : 0, conn->pending_len);
        conn->last_send_tick = now;
        conn->retransmit_count++;

        conn->current_rto *= 2; // RFC 6298 backoff: increase this segment's timeout without changing the RTT estimate
        if (conn->current_rto > TCP_RTO_MAX_TICKS)
        {
            conn->current_rto = TCP_RTO_MAX_TICKS;
        }

        terminal_writestring("TCP: retransmitting\n");
    }
}

// Handles a received TCP segment — handshake, echoing data, and now a clean FIN-based close
void tcp_receive(const uint8_t source_ip[4], const uint8_t source_mac[6], const uint8_t *tcp_data, uint16_t tcp_len)
{
    if (tcp_len < TCP_HEADER_LEN)
    {
        return;
    }

    uint16_t source_port = get_be16(tcp_data, 0);
    uint16_t dest_port = get_be16(tcp_data, 2);
    uint32_t seq = get_be32(tcp_data, 4);
    uint32_t ack = get_be32(tcp_data, 8);
    uint8_t data_offset_bytes = (uint8_t)((tcp_data[12] >> 4) * 4);
    uint8_t flags = tcp_data[13];

    if (dest_port != TCP_TEST_PORT) // only one port is "listening" right now
    {
        return;
    }

    struct tcp_connection *conn = tcp_find_connection(source_ip, source_port);

    if (conn && (flags & TCP_FLAG_RST)) // a reset, no response is ever sent for a RST
    {
        conn->state = TCP_CLOSED; // frees the slot, without this, a client that crashes or force-closes leaves it stuck forever
        conn->pending = 0;
        terminal_writestring("TCP: RST received, connection aborted\n");
        return;
    }

    if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK))
    {
        if (!conn)
        {
            conn = tcp_alloc_connection();
        }
        if (!conn) // table full — a real stack would send RST here; left as a known gap for now
        {
            return;
        }

        for (int i = 0; i < 4; i++)
        {
            conn->remote_ip[i] = source_ip[i];
        }
        for (int i = 0; i < 6; i++)
        {
            conn->remote_mac[i] = source_mac[i];
        }
        conn->local_port = TCP_TEST_PORT;
        conn->remote_port = source_port;
        conn->their_seq = seq + 1; // SYN consumes one sequence number, so the next byte we expect is seq+1
        conn->our_seq = 1000;      // a fixed ISN, not randomized — real TCP randomizes this for security, not correctness
        conn->state = TCP_SYN_RECEIVED;
        conn->pending = 0; // defensive — guarantees a clean slate even if this slot held an aborted connection before
        conn->retransmit_count = 0;
        conn->rx_head = 0;
        conn->rx_tail = 0;
        conn->rx_closed = 0;
        conn->accepted = 0;
        conn->rtt_valid = 0; // Reset the RTT estimate for the new connection

        tcp_send_tracked(conn, TCP_FLAG_SYN | TCP_FLAG_ACK, conn->our_seq, 0, 0);
        conn->our_seq++; // our own SYN also consumes one sequence number
        terminal_writestring("TCP: SYN received, sent SYN-ACK\n");
        return;
    }

    if (!conn)
    {
        return; // a segment for a connection we have no record of — a real stack would RST here too
    }

    if (conn->state == TCP_SYN_RECEIVED && (flags & TCP_FLAG_ACK) && !(flags & TCP_FLAG_SYN))
    {
        if (ack == conn->our_seq && seq == conn->their_seq)
        {
            conn->state = TCP_ESTABLISHED;
            tcp_rtt_sample(conn); // SYN-ACK confirmed; clear retransmission state and record the first RTT sample
            terminal_writestring("TCP: connection established\n");
        }
        return;
    }

    if (conn->state == TCP_ESTABLISHED)
    {
        const uint8_t *payload = tcp_data + data_offset_bytes;
        uint16_t payload_len = (uint16_t)(tcp_len - data_offset_bytes);

        if (payload_len > 0 && seq != conn->their_seq) // out-of-order data — dropped, no reordering support yet
        {
            return;
        }

        if (payload_len > 0)
        {
            conn->their_seq += payload_len; // we've now seen through the end of this segment

            // Queue for whatever userspace program owns this socket — a full buffer just drops the remainder (no flow control yet)
            for (uint16_t i = 0; i < payload_len; i++)
            {
                uint16_t next = (uint16_t)((conn->rx_head + 1) % sizeof(conn->rx_buffer));
                if (next == conn->rx_tail)
                {
                    break;
                }
                conn->rx_buffer[conn->rx_head] = payload[i];
                conn->rx_head = next;
            }
        }

        if (flags & TCP_FLAG_FIN)
        {
            conn->their_seq += 1;         // FIN consumes a sequence number, same as SYN did
            conn->rx_closed = 1;          // signals EOF to a blocked socket_recv() once the queued data is drained
            conn->state = TCP_CLOSE_WAIT; // they're done sending — we might still have data left to send, though

            tcp_send_segment(conn, TCP_FLAG_ACK, conn->our_seq, conn->their_seq, 0, 0); // ack their FIN, no FIN of our own yet
            terminal_writestring("TCP: FIN received, half-closed\n");
            return;
        }

        if (payload_len > 0)
        {
            tcp_send_segment(conn, TCP_FLAG_ACK, conn->our_seq, conn->their_seq, 0, 0); // ack the data — sending it onward is now up to userspace
        }
        else if (conn->pending && (flags & TCP_FLAG_ACK) && ack == conn->our_seq) // a bare ACK confirming our last send
        {
            tcp_rtt_sample(conn);
        }
        return;
    }

    if ((conn->state == TCP_LAST_ACK || conn->state == TCP_CLOSE_WAIT) && (flags & TCP_FLAG_ACK) && ack == conn->our_seq)
    {
        if (conn->state == TCP_LAST_ACK) // only a fully-closed connection frees the slot — CLOSE_WAIT just clears pending sends
        {
            conn->state = TCP_CLOSED;
            terminal_writestring("TCP: connection closed\n");
        }
        if (conn->pending) // Only sample RTT when a segment is actually pending
        {
            tcp_rtt_sample(conn);
        }
        return;
    }
}

// Blocks until some connection reaches ESTABLISHED and hasn't already been claimed, then returns its index
int tcp_socket_accept(void)
{
    __asm__ volatile("sti"); // may be running with IF=0, called from inside a syscall — same as keyboard_read_char

    for (;;)
    {
        for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
        {
            if (connections[i].state == TCP_ESTABLISHED && !connections[i].accepted)
            {
                connections[i].accepted = 1;
                return i;
            }
        }
        __asm__ volatile("hlt");
    }
}

// Blocks until data is available or the connection closes; returns byte count, or 0 once closed and drained
int tcp_socket_recv(int sockfd, uint8_t *buf, uint32_t max_len)
{
    if (sockfd < 0 || sockfd >= MAX_TCP_CONNECTIONS)
    {
        return -1;
    }
    struct tcp_connection *conn = &connections[sockfd];

    __asm__ volatile("sti");
    while (conn->rx_head == conn->rx_tail && !conn->rx_closed && conn->state != TCP_CLOSED)
    {
        __asm__ volatile("hlt");
    }

    uint32_t count = 0;
    while (conn->rx_head != conn->rx_tail && count < max_len)
    {
        buf[count++] = conn->rx_buffer[conn->rx_tail];
        conn->rx_tail = (uint16_t)((conn->rx_tail + 1) % sizeof(conn->rx_buffer));
    }
    return (int)count;
}

// Sends data on a connection that's still able to send (ESTABLISHED or CLOSE_WAIT); returns bytes sent, or -1
int tcp_socket_send(int sockfd, const uint8_t *buf, uint32_t len)
{
    if (sockfd < 0 || sockfd >= MAX_TCP_CONNECTIONS)
    {
        return -1;
    }
    struct tcp_connection *conn = &connections[sockfd];

    if (conn->state != TCP_ESTABLISHED && conn->state != TCP_CLOSE_WAIT)
    {
        return -1;
    }

    if (len > 1460) // one segment's worth, no fragmentation across multiple segments yet
    {
        len = 1460;
    }

    tcp_send_tracked(conn, TCP_FLAG_PSH | TCP_FLAG_ACK, conn->our_seq, buf, (uint16_t)len);
    conn->our_seq += len;
    return (int)len;
}

// Initiates a clean close by sending our own FIN; the slot fully frees once the peer ACKs it
int tcp_socket_close(int sockfd)
{
    if (sockfd < 0 || sockfd >= MAX_TCP_CONNECTIONS)
    {
        return -1;
    }
    struct tcp_connection *conn = &connections[sockfd];

    if (conn->state != TCP_ESTABLISHED && conn->state != TCP_CLOSE_WAIT)
    {
        return -1;
    }

    tcp_send_tracked(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, conn->our_seq, 0, 0);
    conn->our_seq += 1;
    conn->state = TCP_LAST_ACK;
    return 0;
}