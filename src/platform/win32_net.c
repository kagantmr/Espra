#include "net.h"
#include <ws2tcpip.h>
#include <winsock2.h>
#include <unistd.h>
#include "logging.h"
#include <string.h>
#include <stdbool.h>

// Link with the Winsock library automatically in MSVC (if compiling on Windows)
#pragma comment(lib, "ws2_32.lib")

static bool g_winsock_initialized = false;

static bool init_winsock(void) {
    if (g_winsock_initialized) return true;

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }
    g_winsock_initialized = true;
    return true;
}

net_socket_t net_socket(int domain, int type, int protocol) {
    if (!init_winsock()) return -1;

    // Map your abstract parameters to native Windows socket calls
    SOCKET s = socket(domain, type, protocol);
    if (s == INVALID_SOCKET) {
        return -1;
    }
    
    // Cast the Windows SOCKET handle back into your abstract net_socket_t wrapper
    return (net_socket_t)s;
}

int net_bind(net_socket_t sock, const char *ip, port_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // Convert IP string representation to binary network byte format
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return -1;
    }

    if (bind((SOCKET)sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        return -1;
    }
    return 0;
}

int net_listen(net_socket_t sock, int backlog) {
    if (listen((SOCKET)sock, backlog) == SOCKET_ERROR) {
        return -1;
    }
    return 0;
}

net_socket_t net_accept(net_socket_t sock, char *client_ip, size_t ip_len, port_t *client_port) {
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    SOCKET client_s = accept((SOCKET)sock, (struct sockaddr*)&addr, &addr_len);
    if (client_s == INVALID_SOCKET) {
        return -1;
    }

    // Extract human-readable connection metadata if requested
    if (client_ip && ip_len > 0) {
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, (int)ip_len);
    }
    if (client_port) {
        *client_port = ntohs(addr.sin_port);
    }

    return (net_socket_t)client_s;
}

ssize_t net_send(net_socket_t sock, const void *buf, size_t len) {
    // Winsock send expects a const char* and an int length parameter
    int bytes_sent = send((SOCKET)sock, (const char*)buf, (int)len, 0);
    if (bytes_sent == SOCKET_ERROR) {
        return -1;
    }
    return (ssize_t)bytes_sent;
}

ssize_t net_recv(net_socket_t sock, void *buf, size_t len) {
    // Winsock recv expects a char* and an int length parameter
    int bytes_received = recv((SOCKET)sock, (char*)buf, (int)len, 0);
    if (bytes_received == SOCKET_ERROR) {
        return -1;
    }
    return (ssize_t)bytes_received;
}

int net_recv_exact(net_socket_t sock, void *buf, size_t len) {
    size_t total_read = 0;
    char *ptr = (char *)buf;

    while (total_read < len) {
        size_t remaining = len - total_read;
        
        // Read into the buffer, shifting forward by total_read bytes
        ssize_t bytes_read = net_recv(sock, ptr + total_read, remaining);

        if (bytes_read == 0) {
            // Connection was cleanly closed by the peer
            return -1; 
        }
        if (bytes_read < 0) {
            LOG_ERROR("net_recv_exact(): Read error on socket descriptor %d", sock);
            return -2;
        }

        total_read += (size_t)bytes_read;
    }

    return 0; 
}

int net_close(net_socket_t sock) {
    // Windows explicitly demands closesocket() instead of close()
    return closesocket((SOCKET)sock);
}