#include "net.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <logging.h>
#include <string.h>

net_socket_t net_socket(net_domain_t domain, net_type_t type, int protocol)
{
    int os_domain = (domain == NET_AF_INET) ? AF_INET : AF_INET6;
    int os_type = (type == NET_SOCK_STREAM) ? SOCK_STREAM : SOCK_DGRAM;

    int sys_rc = socket(os_domain, os_type, protocol);
    if (sys_rc < 0)
    {
        // We'll replace this with our new LOG_ERROR next!
        LOG_ERROR("net_socket(): socket() failed");
        return NET_INVALID_SOCKET;
    }
    return (net_socket_t)sys_rc;
}

int net_connect(net_socket_t sock, const char *ip, port_t port)
{
    struct sockaddr_in s;
    memset(&s, 0, sizeof(struct sockaddr_in));
    s.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &s.sin_addr) <= 0)
    {
        LOG_ERROR("net_connect(): inet_pton() failed");
        return -1;
    }
    s.sin_family = AF_INET;
    // no need to set sin_len again, usually OS does it for us
    if (connect(sock, (struct sockaddr *)&s, sizeof(s)) < 0)
    {
        LOG_ERROR("net_connect(): connect() failed");
        return -2;
    }

    LOG_INFO("Connected to %s on port %u", ip, port);
    return 0;
}

ssize_t net_send(net_socket_t sock, const void *buf, size_t len)
{
    return send(sock, buf, len, 0);
}

ssize_t net_recv(net_socket_t sock, void *buf, size_t len)
{
    return recv(sock, buf, len, 0);
}

int net_bind(net_socket_t sock, const char *ip, port_t port)
{
    struct sockaddr_in s;
    memset(&s, 0, sizeof(struct sockaddr_in));
    s.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &s.sin_addr) <= 0)
    {
        LOG_ERROR("net_bind(): inet_pton() failed");
        return -1;
    }
    s.sin_family = AF_INET;
    // no need to set sin_len again, usually OS does it for us
    if (bind(sock, (const struct sockaddr *)&s, sizeof(struct sockaddr_in)))
    {
        LOG_ERROR("net_bind(): bind() failed");
        return -2;
    }

    LOG_INFO("Bound to %s on port %u", ip, port);
    return 0;
}

int net_listen(net_socket_t sock, int backlog)
{
    return listen(sock, backlog);
}

net_socket_t net_accept(net_socket_t sock, char *out_client_ip, size_t ip_len, port_t *out_client_port)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd = accept(sock, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
    {
        LOG_ERROR("net_accept(): accept() failed");
        return NET_INVALID_SOCKET;
    }

    // Safely extract port if the caller provided a destination pointer
    if (out_client_port != NULL)
    {
        *out_client_port = ntohs(client_addr.sin_port);
    }

    // Safely extract IP string if the caller provided a destination pointer
    if (out_client_ip != NULL && ip_len > 0)
    {
        if (inet_ntop(AF_INET, &client_addr.sin_addr, out_client_ip, (socklen_t)ip_len) == NULL)
        {
            LOG_ERROR("net_accept(): inet_ntop() failed to convert client IP");
            // We can still return the socket even if logging details failed
        }
    }

    // Log the incoming connection using the proper type specifier (%u for port_t/uint32_t)
    if (out_client_ip != NULL && out_client_port != NULL)
    {
        LOG_INFO("Accepted connection from %s on port %u", out_client_ip, *out_client_port);
    }
    else
    {
        LOG_INFO("Accepted incoming connection (metadata not requested)");
    }

    return (net_socket_t)client_fd;
}

int net_recv_exact(net_socket_t sock, void *buf, size_t len)
{
    size_t total_read = 0;
    char *ptr = (char *)buf;

    while (total_read < len)
    {
        size_t remaining = len - total_read;

        // Read into the buffer, shifting forward by total_read bytes
        ssize_t bytes_read = net_recv(sock, ptr + total_read, remaining);

        if (bytes_read == 0)
        {
            // Connection was cleanly closed by the peer
            return -1;
        }
        if (bytes_read < 0)
        {
            LOG_ERROR("net_recv_exact(): Read error on socket descriptor %d", sock);
            return -2;
        }

        total_read += (size_t)bytes_read;
    }

    return 0;
}

int net_close(net_socket_t sock)
{
    return close(sock);
}