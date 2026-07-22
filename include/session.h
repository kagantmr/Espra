/**
 * ESPRA server side session details
 */

#pragma once

#include "net.h"
#include "protocol.h"
#include <stdbool.h>

typedef struct session
{
    net_socket_t client_sock;             // client's socket ID
    char username[ESPRA_NAME_MAX];        // username
    char current_channel[ESPRA_NAME_MAX]; // channel name
    bool is_active;                       // is the user currently active
} session_t;