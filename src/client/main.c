#include "net.h"
#include "logging.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define CLIENT_BUF_SZ 1024

int main(void) {


    char name[ESPRA_NAME_MAX];
    printf("Enter your name: ");
    fflush(stdout);
    if (fgets(name, ESPRA_NAME_MAX, stdin) != NULL) {
        // Strip the trailing newline character that fgets appends
        name[strcspn(name, "\n")] = '\0';
    }

    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0) {
        return sock;
    }
    if (net_connect(sock, "127.0.0.1", ESPRA_PORT) != 0) {
        return -1;
    }

    char input_buf[CLIENT_BUF_SZ];
    char reply_buf[CLIENT_BUF_SZ];
    espra_header_t header;
    header.command = ESPRA_CMD_AUTH_REQ;
    header.flags = 0;
    header.packet_len = sizeof(espra_header_t) + ESPRA_NAME_MAX + ESPRA_PWD_MAX;

    char payload[ESPRA_NAME_MAX + ESPRA_PWD_MAX];
    strncpy(payload, name, ESPRA_NAME_MAX);
    strncpy(payload + ESPRA_NAME_MAX, ESPRA_GLOBAL_PASSWORD, ESPRA_PWD_MAX);

    packet_write(sock, &header, payload);

    espra_header_t serv_hdr;

    packet_read(sock, &serv_hdr, reply_buf, CLIENT_BUF_SZ);

    if (serv_hdr.command != ESPRA_CMD_AUTH_RESP) {
        LOG_ERROR("Authentication rejected by server!");
        net_close(sock);
        return -1;
    }

    while(1) {
        if (fgets(input_buf, CLIENT_BUF_SZ, stdin) == NULL) {
            break;
        }
        printf("%s said: %s", name, input_buf);
        // net_send(sock, input_buf, strnlen(input_buf, CLIENT_BUF_SZ));
        espra_header_t header;
        header.command = ESPRA_CMD_BCAST;
        header.flags = 0;
        header.packet_len = sizeof(espra_header_t) + strnlen(input_buf, CLIENT_BUF_SZ);
        packet_write(sock, &header, input_buf);

        espra_header_t serv_hdr;

        packet_status_t status = packet_read(sock, &serv_hdr, reply_buf, CLIENT_BUF_SZ);
        if (status != PACKET_OK) {
            LOG_INFO("Disconnected from server or connection lost.");
            break;
        }
        size_t payload_len = serv_hdr.packet_len - sizeof(espra_header_t);
        if (payload_len < CLIENT_BUF_SZ) {
            reply_buf[payload_len] = '\0';
        }

        switch(serv_hdr.command) {
            case ESPRA_CMD_BCAST: {
                printf("They said: %s", reply_buf);
            } break;
            case ESPRA_CMD_DMSG: {
                printf("They said (privately): %s", reply_buf);
            } break;
            default: {
                printf("They sent an unknown command\n");
            } break;
        }
    }

    net_close(sock);
    return 0;
}