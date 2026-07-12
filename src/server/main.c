#include "net.h"
#include "logging.h"
#include <string.h>
#include <stdio.h>

#define SERVER_BUF_SZ 4096

int main(void) {
    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0) {
        return sock;
    }

    char reply_buf[SERVER_BUF_SZ];

    net_bind(sock, "0.0.0.0", ESPRA_PORT);
    net_listen(sock, 0);

    char client_ip[16];
    port_t client_port;
    net_socket_t client_sock = net_accept(sock, client_ip, 16, &client_port);

    while(1) {
        ssize_t sz = net_recv(client_sock, reply_buf, SERVER_BUF_SZ - 1);
        if (sz > 0) {
            reply_buf[sz] = '\0';
        } else {
            LOG_INFO("Disconnected...");
            break;
        }
        LOG_INFO("They said: %s", reply_buf);
        net_send(client_sock, reply_buf, strnlen(reply_buf, SERVER_BUF_SZ));
    }

    net_close(client_sock);
    net_close(sock);
    return 0;
}