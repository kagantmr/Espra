#include "net.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(void) {
    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0) {
        return sock;
    }

    if (net_connect(sock, "127.0.0.1", 8080) != 0) {
        return -1;
    }

    net_send(sock, "Hello network! :wink:", 22);

    net_close(sock);
    return 0;
}