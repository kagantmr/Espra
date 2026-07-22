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

// Sends a plain-text notice packet (SYS / INFO / ERR / JOIN / NICK echoes) to a
// single socket. Only the owning client's handler thread writes to that socket,
// so no lock is needed here.
static void send_notice(net_socket_t sock, espra_command_t cmd, const char *text)
{
    espra_header_t h = {0};
    h.command = cmd;
    h.packet_len = sizeof(espra_header_t) + strnlen(text, SERVER_BUF_SZ);
    packet_write(sock, &h, text);
}

// Delivers a packet to every active session in `channel` except `except`.
// The registry_lock MUST be held by the caller.
static void channel_broadcast(const char *channel, net_socket_t except,
                              espra_command_t cmd, const void *payload,
                              size_t payload_len)
{
    espra_header_t h = {0};
    h.command = cmd;
    h.packet_len = sizeof(espra_header_t) + payload_len;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (sessions[i].is_active &&
            sessions[i].client_sock != except &&
            strncmp(sessions[i].current_channel, channel, ESPRA_CHANNEL_MAX) == 0)
        {
            packet_write(sessions[i].client_sock, &h, payload);
        }
    }
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
            // Relay to the sender's channel as [name field: sender][text].
            char out[ESPRA_NAME_MAX + SERVER_BUF_SZ];
            memset(out, 0, ESPRA_NAME_MAX);
            strncpy(out, session->username, ESPRA_NAME_MAX - 1);
            size_t tlen = strnlen(payload, SERVER_BUF_SZ - 1);
            memcpy(out + ESPRA_NAME_MAX, payload, tlen);

            espra_mutex_lock(registry_lock);
            channel_broadcast(session->current_channel, session->client_sock,
                              ESPRA_CMD_BCAST, out, ESPRA_NAME_MAX + tlen);
            espra_mutex_unlock(registry_lock);
        }
        break;
        case ESPRA_CMD_DMSG:
        {
            // Incoming: [name field: target][text]. Relay: [name field: sender][text].
            char target[ESPRA_NAME_MAX] = {0};
            size_t nlen = payload_len < ESPRA_NAME_MAX ? payload_len : ESPRA_NAME_MAX;
            memcpy(target, payload, nlen);
            target[ESPRA_NAME_MAX - 1] = '\0';

            const char *text = payload + ESPRA_NAME_MAX;
            size_t tlen = payload_len > ESPRA_NAME_MAX ? payload_len - ESPRA_NAME_MAX : 0;

            char out[ESPRA_NAME_MAX + SERVER_BUF_SZ];
            memset(out, 0, ESPRA_NAME_MAX);
            strncpy(out, session->username, ESPRA_NAME_MAX - 1);
            memcpy(out + ESPRA_NAME_MAX, text, tlen);

            espra_header_t dm_hdr = {0};
            dm_hdr.command = ESPRA_CMD_DMSG;
            dm_hdr.packet_len = sizeof(espra_header_t) + ESPRA_NAME_MAX + tlen;

            bool delivered = false;
            espra_mutex_lock(registry_lock);
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (sessions[i].is_active &&
                    strncmp(sessions[i].username, target, ESPRA_NAME_MAX) == 0)
                {
                    packet_write(sessions[i].client_sock, &dm_hdr, out);
                    delivered = true;
                }
            }
            espra_mutex_unlock(registry_lock);

            if (!delivered)
            {
                char err[ESPRA_NAME_MAX + 32];
                snprintf(err, sizeof(err), "No such user: %s", target);
                send_notice(session->client_sock, ESPRA_CMD_ERR, err);
            }
        }
        break;
        case ESPRA_CMD_JOIN:
        {
            // Payload is the target channel name (a leading '#' is tolerated).
            char newchan[ESPRA_CHANNEL_MAX] = {0};
            const char *src = payload;
            if (src[0] == '#')
                src++;
            strncpy(newchan, src, ESPRA_CHANNEL_MAX - 1);
            char *sp = strchr(newchan, ' ');
            if (sp)
                *sp = '\0';

            if (newchan[0] == '\0')
            {
                send_notice(session->client_sock, ESPRA_CMD_ERR, "Usage: /join <channel>");
                break;
            }

            espra_mutex_lock(registry_lock);
            if (strncmp(session->current_channel, newchan, ESPRA_CHANNEL_MAX) == 0)
            {
                espra_mutex_unlock(registry_lock);
                send_notice(session->client_sock, ESPRA_CMD_INFO, "You are already in that channel.");
                break;
            }

            char notice[ESPRA_NAME_MAX + ESPRA_CHANNEL_MAX + 32];
            snprintf(notice, sizeof(notice), "%s left #%s",
                     session->username, session->current_channel);
            channel_broadcast(session->current_channel, session->client_sock,
                              ESPRA_CMD_SYS, notice, strlen(notice));

            memset(session->current_channel, 0, ESPRA_CHANNEL_MAX);
            strncpy(session->current_channel, newchan, ESPRA_CHANNEL_MAX - 1);

            snprintf(notice, sizeof(notice), "%s joined #%s",
                     session->username, session->current_channel);
            channel_broadcast(session->current_channel, session->client_sock,
                              ESPRA_CMD_SYS, notice, strlen(notice));
            espra_mutex_unlock(registry_lock);

            // Echo the authoritative channel back so the joiner's UI switches.
            send_notice(session->client_sock, ESPRA_CMD_JOIN, session->current_channel);
        }
        break;
        case ESPRA_CMD_NICK:
        {
            char newname[ESPRA_NAME_MAX] = {0};
            strncpy(newname, payload, ESPRA_NAME_MAX - 1);
            char *sp = strchr(newname, ' ');
            if (sp)
                *sp = '\0';

            if (newname[0] == '\0')
            {
                send_notice(session->client_sock, ESPRA_CMD_ERR, "Usage: /nick <newname>");
                break;
            }

            espra_mutex_lock(registry_lock);
            bool taken = false;
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (sessions[i].is_active &&
                    sessions[i].client_sock != session->client_sock &&
                    strncmp(sessions[i].username, newname, ESPRA_NAME_MAX) == 0)
                {
                    taken = true;
                    break;
                }
            }
            if (taken)
            {
                espra_mutex_unlock(registry_lock);
                send_notice(session->client_sock, ESPRA_CMD_ERR, "That name is already taken.");
                break;
            }

            char oldname[ESPRA_NAME_MAX];
            memcpy(oldname, session->username, ESPRA_NAME_MAX);
            oldname[ESPRA_NAME_MAX - 1] = '\0';

            memset(session->username, 0, ESPRA_NAME_MAX);
            strncpy(session->username, newname, ESPRA_NAME_MAX - 1);

            char notice[ESPRA_NAME_MAX * 2 + 32];
            snprintf(notice, sizeof(notice), "%s is now known as %s", oldname, newname);
            channel_broadcast(session->current_channel, session->client_sock,
                              ESPRA_CMD_SYS, notice, strlen(notice));
            espra_mutex_unlock(registry_lock);

            // Confirm to the requester so their client updates ctx->name.
            send_notice(session->client_sock, ESPRA_CMD_NICK, newname);
        }
        break;
        case ESPRA_CMD_LIST:
        {
            char buf[SERVER_BUF_SZ];
            size_t off = 0;

            espra_mutex_lock(registry_lock);
            off += snprintf(buf + off, sizeof(buf) - off,
                            "Users in #%s:", session->current_channel);
            for (int i = 0; i < MAX_CLIENTS && off < sizeof(buf); i++)
            {
                if (sessions[i].is_active &&
                    strncmp(sessions[i].current_channel, session->current_channel, ESPRA_CHANNEL_MAX) == 0)
                {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                    " %.*s", ESPRA_NAME_MAX, sessions[i].username);
                }
            }
            if (off < sizeof(buf))
                off += snprintf(buf + off, sizeof(buf) - off, "\nChannels:");
            for (int i = 0; i < MAX_CLIENTS && off < sizeof(buf); i++)
            {
                if (!sessions[i].is_active)
                    continue;
                bool seen = false; // only list each distinct channel once
                for (int j = 0; j < i; j++)
                    if (sessions[j].is_active &&
                        strncmp(sessions[j].current_channel, sessions[i].current_channel, ESPRA_CHANNEL_MAX) == 0)
                    {
                        seen = true;
                        break;
                    }
                if (seen)
                    continue;
                int count = 0;
                for (int j = 0; j < MAX_CLIENTS; j++)
                    if (sessions[j].is_active &&
                        strncmp(sessions[j].current_channel, sessions[i].current_channel, ESPRA_CHANNEL_MAX) == 0)
                        count++;
                off += snprintf(buf + off, sizeof(buf) - off,
                                " #%s(%d)", sessions[i].current_channel, count);
            }
            espra_mutex_unlock(registry_lock);

            send_notice(session->client_sock, ESPRA_CMD_INFO, buf);
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

    char sys_payload[ESPRA_NAME_MAX + 32];
    snprintf(sys_payload, sizeof(sys_payload), "%s left the chat.", session->username);
    channel_broadcast(session->current_channel, session->client_sock,
                      ESPRA_CMD_SYS, sys_payload, strlen(sys_payload));

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
        memset(session->current_channel, 0, ESPRA_CHANNEL_MAX);
        strncpy(session->current_channel, ESPRA_DEFAULT_CHANNEL, ESPRA_CHANNEL_MAX - 1);

        char sys_payload[ESPRA_NAME_MAX + ESPRA_CHANNEL_MAX + 32];
        snprintf(sys_payload, sizeof(sys_payload), "%.*s joined #%s",
                 ESPRA_NAME_MAX, username, session->current_channel);
        channel_broadcast(session->current_channel, client_sock,
                          ESPRA_CMD_SYS, sys_payload, strlen(sys_payload));

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