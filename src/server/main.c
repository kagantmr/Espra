#include "net.h"
#include "logging.h"
#include "protocol.h"
#include "thread.h"
#include "session.h"
#include <string.h>
#include <stdio.h>

#define SERVER_BUF_SZ 4096
#define MAX_CLIENTS 32

session_t sessions[MAX_CLIENTS];
espra_mutex_t *registry_lock;

int session_find_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!sessions[i].is_active)
            return i;
    return -1;
}

bool is_username_taken(const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sessions[i].is_active && strncmp(sessions[i].username, name, ESPRA_NAME_MAX) == 0)
        {
            return true;
        }
    }
    return false;
}

void *client_handler(void *arg)
{
    // Unpack the argument
    session_t *session = (session_t *)arg;

    // The entire inner while(1) loop you wrote goes here!
    // Inside this function, use session->client_sock instead of client_sock.

    while (1)
    {
        // ssize_t sz = net_recv(client_sock, reply_buf, SERVER_BUF_SZ - 1);
        espra_header_t client_hdr;
        char payload[SERVER_BUF_SZ];

        int status = packet_read(session->client_sock, &client_hdr, payload, SERVER_BUF_SZ);
        if (status != PACKET_OK)
        {
            LOG_INFO("Disconnected from client or connection lost.");
            break;
        }

        size_t payload_len = client_hdr.packet_len - sizeof(espra_header_t);
        payload[payload_len] = '\0';

        switch (client_hdr.command)
        {
        case ESPRA_CMD_BCAST:
        {

            char formatted[SERVER_BUF_SZ];
            snprintf(formatted, sizeof(formatted), "[%s]: %s", session->username, payload);
            client_hdr.packet_len = sizeof(espra_header_t) + strnlen(formatted, SERVER_BUF_SZ);

            espra_mutex_lock(registry_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (sessions[i].is_active && sessions[i].client_sock != session->client_sock)
                {
                    if (packet_write(sessions[i].client_sock, &client_hdr, formatted) != PACKET_OK)
                    {
                        continue;
                    }
                }
            }
            espra_mutex_unlock(registry_lock);
        }
        break;
        case ESPRA_CMD_DMSG:
        {
            const char *target_name = payload;

            espra_mutex_lock(registry_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (sessions[i].is_active && strncmp(sessions[i].username, target_name, ESPRA_NAME_MAX) == 0)
                {
                    if (packet_write(sessions[i].client_sock, &client_hdr, payload) != PACKET_OK)
                    {
                        continue;
                    }
                }
            }
            espra_mutex_unlock(registry_lock);
        }
        break;
        default:
        {
            printf("They sent an unknown command\n");
        }
        break;
        }
    }

    espra_mutex_lock(registry_lock);

    espra_header_t sys_hdr = {0};
    char sys_payload[SERVER_BUF_SZ];
    snprintf(sys_payload, sizeof(sys_payload), "[System]: %s left the chat.", session->username);

    sys_hdr.command = ESPRA_CMD_BCAST;
    sys_hdr.packet_len = sizeof(espra_header_t) + strlen(sys_payload);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sessions[i].is_active && sessions[i].client_sock != session->client_sock)
        {
            packet_write(sessions[i].client_sock, &sys_hdr, sys_payload);
        }
    }

    session->is_active = false;
    net_close(session->client_sock);
    espra_mutex_unlock(registry_lock);

    return NULL;
}

int main(void)
{

    registry_lock = espra_mutex_create();

    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0)
    {
        return sock;
    }

    char reply_buf[SERVER_BUF_SZ];

    net_bind(sock, "0.0.0.0", ESPRA_PORT);
    net_listen(sock, 0);

    while (1)
    {
        char client_ip[16];
        port_t client_port;
        net_socket_t client_sock = net_accept(sock, client_ip, 16, &client_port);

        espra_header_t client_hdr;
        char payload[ESPRA_NAME_MAX + ESPRA_PWD_MAX + 1];

        if (packet_read(client_sock, &client_hdr, payload, sizeof(payload)) != 0)
        {
            net_close(client_sock);
            continue;
        }

        size_t payload_len = client_hdr.packet_len - sizeof(espra_header_t);
        payload[payload_len] = '\0';

        if (client_hdr.command != ESPRA_CMD_AUTH_REQ)
        {
            net_close(client_sock);
            continue;
        }

        const char *username = (const char *)payload; // use ESPRA_NAME_MAX bytes from it at ALL COSTS!
        const char *pwd = (const char *)payload + ESPRA_NAME_MAX;

        if (strncmp(pwd, ESPRA_GLOBAL_PASSWORD, ESPRA_PWD_MAX) != 0)
        {

            espra_header_t reply_hdr = {0};

            reply_hdr.command = ESPRA_CMD_AUTH_RESP;
            reply_hdr.flags |= ESPRA_ERROR;
            reply_hdr.packet_len = sizeof(reply_hdr) + 1; // error code payload

            uint8_t err_code = AUTH_ERR_PWD;

            packet_write(client_sock, &reply_hdr, &err_code);

            net_close(client_sock);
            continue;
        }

        espra_mutex_lock(registry_lock);

        if (is_username_taken(username))
        {

            espra_header_t reply_hdr = {0};

            reply_hdr.command = ESPRA_CMD_AUTH_RESP;
            reply_hdr.flags |= ESPRA_ERROR;
            reply_hdr.packet_len = sizeof(reply_hdr) + 1; // error code payload

            uint8_t err_code = AUTH_ERR_TAKEN;

            packet_write(client_sock, &reply_hdr, &err_code);

            espra_mutex_unlock(registry_lock);
            net_close(client_sock);
            continue;
        }

        espra_header_t reply_hdr = {0};

        reply_hdr.command = ESPRA_CMD_AUTH_RESP;
        reply_hdr.packet_len = sizeof(espra_header_t);

        packet_write(client_sock, &reply_hdr, reply_buf);

        int slot = session_find_slot();
        if (slot < 0)
        {

            espra_header_t reply_hdr = {0};

            reply_hdr.command = ESPRA_CMD_AUTH_RESP;
            reply_hdr.flags |= ESPRA_ERROR;
            reply_hdr.packet_len = sizeof(reply_hdr) + 1; // error code payload

            uint8_t err_code = AUTH_ERR_FULL;

            packet_write(client_sock, &reply_hdr, &err_code);

            espra_mutex_unlock(registry_lock);
            net_close(client_sock);
            continue;
        }

        session_t *session = &sessions[slot];
        session->client_sock = client_sock;
        session->is_active = true;
        strncpy(session->username, username, ESPRA_NAME_MAX);

        espra_header_t sys_hdr = {0};
        char sys_payload[SERVER_BUF_SZ];
        snprintf(sys_payload, sizeof(sys_payload), "%s joined the chat!", username);

        sys_hdr.command = ESPRA_CMD_BCAST;
        sys_hdr.packet_len = sizeof(espra_header_t) + strlen(sys_payload);

        // Sweep the array and tell everyone else
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (sessions[i].is_active && sessions[i].client_sock != client_sock)
            {
                packet_write(sessions[i].client_sock, &sys_hdr, sys_payload);
            }
        }

        espra_mutex_unlock(registry_lock);

        espra_thread_t *thread = espra_thread_create(client_handler, &sessions[slot]);
        if (thread != NULL)
        {
            espra_thread_detach(thread);
            espra_thread_free(thread); // Free the abstract wrapper handle allocation
        }
    }

    net_close(sock);
    return 0;
}