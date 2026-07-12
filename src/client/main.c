#include "net.h"
#include "logging.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CLIENT_BUF_SZ 1024
#define CLIENT_NAME_MAX 16

int main(void) {
    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0) {
        return sock;
    }
    if (net_connect(sock, "127.0.0.1", ESPRA_PORT) != 0) {
        return -1;
    }

    char name[CLIENT_NAME_MAX];
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(name, CLIENT_NAME_MAX, stdin) != NULL) {
        // Strip the trailing newline character that fgets appends
        name[strcspn(name, "\n")] = '\0';
    }

    char input_buf[CLIENT_BUF_SZ];
    char reply_buf[CLIENT_BUF_SZ];

    while(1) {
        if (fgets(input_buf, CLIENT_BUF_SZ, stdin) == NULL) {
            break;
        }
        LOG_INFO("%s said: %s", name, input_buf);
        net_send(sock, input_buf, strnlen(input_buf, CLIENT_BUF_SZ));
        ssize_t sz = net_recv(sock, reply_buf, CLIENT_BUF_SZ - 1);
        if (sz > 0) {
            reply_buf[sz] = '\0';
        } else {
            LOG_INFO("Disconnected...");
            break;
        }
        LOG_INFO("They said: %s", reply_buf);
        
    }

    close(sock);
    return 0;
}