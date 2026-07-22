/**
 * ESPRA chat protocol
 */

#pragma once

#include "net.h"
#include <stdint.h>

typedef enum
{
    ESPRA_CMD_NONE = 0,
    ESPRA_CMD_AUTH_REQ,
    ESPRA_CMD_AUTH_RESP,
    ESPRA_CMD_BCAST,
    ESPRA_CMD_DMSG,
    ESPRA_CMD_SYS,
    ESPRA_CMD_INFO,
    ESPRA_CMD_ADMIN,
    ESPRA_CMD_ERR
} espra_command_t;

typedef enum
{
    AUTH_SUCCESS = 0, // successfully in
    AUTH_ERR_PWD,     // wrong global password (later on, wrong password)
    AUTH_ERR_TAKEN,   // username taken
    AUTH_ERR_FULL     // server full
} auth_status_t;

typedef struct
{
    uint32_t packet_len;     // Total length of the packet (header + payload) in bytes
    espra_command_t command; // Action identifier
    uint8_t flags;           // Metadata bitflags
    uint16_t reserved;       // Reserved space for future architecture expansion (keeps 32-bit alignment)
} __attribute__((packed)) espra_header_t;

#define ESPRA_GLOBAL_PASSWORD "zuzu123"
#define ESPRA_NAME_MAX 16
#define ESPRA_PWD_MAX 64

#define ESPRA_COMPRESSED (1u << 1)
#define ESPRA_ENCRYPTED (1u << 2)
#define ESPRA_ERROR (1u << 8)

typedef enum
{
    PACKET_OK = 0,
    PACKET_DISCONNECTED,
    PACKET_ERR_HEADER,
    PACKET_ERR_PAYLOAD
} packet_status_t;

// Reads a complete frame out of a stateful socket stream
packet_status_t packet_read(net_socket_t sock, espra_header_t *out_header, void *payload_buf, size_t max_payload_len);

// Writes a frame into a socket stream
packet_status_t packet_write(net_socket_t sock, const espra_header_t *header, const void *payload);