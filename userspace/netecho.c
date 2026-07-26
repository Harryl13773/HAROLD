// Accepts one TCP connection on port 7 and echoes data back until the client closes it

#include "libc.h"

void _start(void)
{
    const char *waiting_msg = "netecho: waiting for a connection\n";
    write(waiting_msg, strlen(waiting_msg));

    int sockfd = socket_accept();

    const char *connected_msg = "netecho: client connected\n";
    write(connected_msg, strlen(connected_msg));

    char buf[512];
    int n;
    while ((n = socket_recv(sockfd, buf, sizeof(buf))) > 0)
    {
        socket_send(sockfd, buf, (unsigned int)n);
    }

    socket_close(sockfd);

    const char *done_msg = "netecho: connection closed, exiting\n";
    write(done_msg, strlen(done_msg));

    exit();

    for (;;)
    {
    }
}