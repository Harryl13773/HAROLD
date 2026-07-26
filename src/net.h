#ifndef NET_H
#define NET_H

// Checks the NIC once for a waiting frame and dispatches it (ARP or IP) if there is one
void net_poll(void);

// Runs net_poll forever as its own task, so the responder stays alive alongside the shell
void net_task(void);

#endif