/**
 * Generic logging utilities.
 */

#pragma once

#include <stdio.h>

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

// Standard C99 safe macro using a single multi-statement block
#define LOG_MESSAGE(level, prefix, ...) \
    do { \
        fprintf((level == LOG_LEVEL_ERROR) ? stderr : stdout, "[%s] [%s:%d] ", prefix, __FILE__, __LINE__); \
        fprintf((level == LOG_LEVEL_ERROR) ? stderr : stdout, __VA_ARGS__); \
        fprintf((level == LOG_LEVEL_ERROR) ? stderr : stdout, "\n"); \
    } while(0)

#define LOG_INFO(...)  LOG_MESSAGE(LOG_LEVEL_INFO,  "INFO",  __VA_ARGS__)
#define LOG_WARN(...)  LOG_MESSAGE(LOG_LEVEL_WARN,  "WARN",  __VA_ARGS__)
#define LOG_ERROR(...) LOG_MESSAGE(LOG_LEVEL_ERROR, "ERROR", __VA_ARGS__)