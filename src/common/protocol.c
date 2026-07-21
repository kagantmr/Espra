#include "protocol.h"
#include <string.h>
#include "logging.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

packet_status_t packet_read(net_socket_t sock, espra_header_t *out_header, void *payload_buf, size_t max_payload_len) {
    // Read the exact size of the fixed header
    if (net_recv_exact(sock, out_header, sizeof(espra_header_t)) != 0) {
        return PACKET_DISCONNECTED;
    }

    // fix network endianness to host endianness for processing
    out_header->packet_len = ntohl(out_header->packet_len);

    // Calculate dynamic payload size
    if (out_header->packet_len < sizeof(espra_header_t)) {
        return PACKET_ERR_HEADER;
    }
    size_t payload_len = out_header->packet_len - sizeof(espra_header_t);

    // Guard against buffer overflows
    if (payload_len > max_payload_len) {
        LOG_ERROR("packet_read(): Payload size %zu exceeds buffer limit %zu", payload_len, max_payload_len);
        return PACKET_ERR_PAYLOAD;
    }

    if (payload_len > 0) {
        if (net_recv_exact(sock, payload_buf, payload_len) != 0) {
            return PACKET_DISCONNECTED;
        }
    }

    return PACKET_OK;
}

packet_status_t packet_write(net_socket_t sock, const espra_header_t *header, const void *payload) {
    espra_header_t hdr;
    memcpy(&hdr, header, sizeof(espra_header_t));
    hdr.packet_len = htonl(header->packet_len);
    
    net_send(sock, &hdr, sizeof(hdr));
    net_send(sock, payload, header->packet_len - sizeof(hdr));

    return PACKET_OK;
}