#pragma once

#include "net.h"
#include <stdint.h>

typedef struct {
    uint32_t packet_len; // Total length of the packet (Header + Payload) in bytes
    uint8_t  command;    // Action identifier (e.g., 1 = Auth, 2 = Broadcast Msg, 3 = Direct Msg)
    uint8_t  flags;      // Metadata bitflags (e.g., Bit 0: Encrypted, Bit 1: Compressed)
    uint16_t reserved;   // Reserved space for future architecture expansion (keeps 32-bit alignment)
} __attribute__((packed)) espra_header_t; 

#pragma once
// A clean configuration macro for your shared password strategy
#define ESPRA_GLOBAL_PASSWORD "zuzu123"

// Unique status codes for packet processing outcomes
typedef enum {
    PACKET_OK = 0,
    PACKET_DISCONNECTED,
    PACKET_ERR_HEADER,
    PACKET_ERR_PAYLOAD
} packet_status_t;

// Reads a complete frame (Header + Payload) out of a stateful socket stream
packet_status_t packet_read(net_socket_t sock, espra_header_t *out_header, void *payload_buf, size_t max_payload_len);