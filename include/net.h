/**
 * OS neutral socket interface implementation.
 * Implement the functions in this header for different operating systems
 * and the program will work.
 */

#pragma once

#include <stddef.h>
#include <sys/types.h> 
#include <stdint.h>

typedef int32_t net_socket_t;
typedef uint32_t port_t;

#define NET_INVALID_SOCKET (-1)
#define ESPRA_PORT 8080

typedef enum {
    NET_AF_INET,   // IPv4
    NET_AF_INET6   // IPv6 (for future expansion)
} net_domain_t;

typedef enum {
    NET_SOCK_STREAM, // TCP
    NET_SOCK_DGRAM   // UDP
} net_type_t;

// Creates a socket. Returns the socket handle, or NET_INVALID_SOCKET on failure.
net_socket_t net_socket(net_domain_t domain, net_type_t type, int protocol);

// Connects to a remote host. Returns 0 on success, negative on failure.
int net_connect(net_socket_t sock, const char *ip, port_t port);

// Sends data. Returns bytes sent, or negative on failure.
ssize_t net_send(net_socket_t sock, const void *buf, size_t len);

// Receives data. Returns bytes received, 0 on connection close, or negative on failure.
ssize_t net_recv(net_socket_t sock, void *buf, size_t len);

// Binds a socket to a specific local IP and port. Returns 0 on success.
int net_bind(net_socket_t sock, const char *ip, port_t port);

// Puts the socket into passive listening mode. Returns 0 on success.
int net_listen(net_socket_t sock, int backlog);

// Accepts an incoming client connection. 
// Returns a new socket handle for client communication, or NET_INVALID_SOCKET.
net_socket_t net_accept(net_socket_t sock, char *out_client_ip, size_t ip_len, port_t *out_client_port);

// Loops internally until 'len' bytes are completely read from the socket.
// Returns 0 on success, or a negative value if the connection drops.
int net_recv_exact(net_socket_t sock, void *buf, size_t len);

// Closes an open connection.
int net_close(net_socket_t sock);