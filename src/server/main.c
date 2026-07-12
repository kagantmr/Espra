#include "net.h"
#include "logging.h"
#include "protocol.h"
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

    while (1) {
        char client_ip[16];
        port_t client_port;
        net_socket_t client_sock = net_accept(sock, client_ip, 16, &client_port);

        espra_header_t client_hdr;
        char payload[ESPRA_NAME_MAX + ESPRA_PWD_MAX + 1];

        if (packet_read(client_sock, &client_hdr, payload, sizeof(payload)) != 0) {
            net_close(client_sock);
            continue;
        }
            
        size_t payload_len = client_hdr.packet_len - sizeof(espra_header_t);
        payload[payload_len] = '\0';

        if (client_hdr.command != ESPRA_CMD_AUTH_REQ) {
            net_close(client_sock);
            continue;
        } 
        
        const char *pwd = (const char*)payload + ESPRA_NAME_MAX;
            
        if (strncmp(pwd, ESPRA_GLOBAL_PASSWORD, ESPRA_PWD_MAX) != 0) {
            net_close(client_sock);
            continue;
        }

        espra_header_t reply_hdr;

        reply_hdr.command = ESPRA_CMD_AUTH_RESP;
        reply_hdr.packet_len = sizeof(espra_header_t);
        
        packet_write(client_sock, &reply_hdr, reply_buf);

        while(1) {
            // ssize_t sz = net_recv(client_sock, reply_buf, SERVER_BUF_SZ - 1);
            espra_header_t client_hdr;
            char payload[SERVER_BUF_SZ];

            int status = packet_read(client_sock, &client_hdr, payload, SERVER_BUF_SZ);
            if (status != PACKET_OK) {
                LOG_INFO("Disconnected from client or connection lost.");
                break;
            }

            size_t payload_len = client_hdr.packet_len - sizeof(espra_header_t);
            payload[payload_len] = '\0';

            switch(client_hdr.command) {
                case ESPRA_CMD_BCAST: {
                    printf("They said: %s", payload);
                } break;
                case ESPRA_CMD_DMSG: {
                    printf("They said (privately): %s", payload);
                } break;
                default: {
                    printf("They sent an unknown command\n");
                } break;
            }

            espra_header_t reply_hdr;

            reply_hdr.command = client_hdr.command;
            reply_hdr.packet_len = sizeof(espra_header_t) + strnlen(payload, SERVER_BUF_SZ);

            packet_write(client_sock, &reply_hdr, payload);
        }

        net_close(client_sock);
    }

    net_close(sock);
    return 0;
}