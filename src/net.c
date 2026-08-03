// Network polling loop: pulls frames off the NIC and dispatches them to ARP/IP, runs as a background task

#include <stdint.h>
#include "rtl8139.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "task.h"
#include "net.h"

#define ETHERTYPE_ARP_HIGH 0x08
#define ETHERTYPE_ARP_LOW 0x06
#define ETHERTYPE_IP_HIGH 0x08
#define ETHERTYPE_IP_LOW 0x00

// Checks the NIC once for a waiting frame and dispatches it (ARP or IP) if there is one
void net_poll(void)
{
    uint8_t frame[1518]; // max Ethernet frame plus a little slack, matching the driver's own tx_buffer sizing

    int len = rtl8139_receive_packet(frame, sizeof(frame));
    if (len < 14) // 0 means nothing waiting; anything under a full Ethernet header isn't usable either way
    {
        return;
    }

    if (frame[12] == ETHERTYPE_ARP_HIGH && frame[13] == ETHERTYPE_ARP_LOW)
    {
        uint8_t our_ip[4];
        ip_get_address(our_ip);
        arp_respond_if_request(frame, len, our_ip);
    }
    else if (frame[12] == ETHERTYPE_IP_HIGH && frame[13] == ETHERTYPE_IP_LOW)
    {
        ip_receive(frame, len);
    }
}

// Runs net_poll forever as its own task, so the responder stays alive alongside the shell
void net_task(void)
{
    for (;;)
    {
        net_poll();
        tcp_check_retransmits();
        task_sleep(10); // check ~100 times a second — responsive enough for interactive ping, not CPU-greedy
    }
}