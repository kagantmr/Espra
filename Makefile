CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c99 -Iinclude
LDFLAGS = 

OBJ_DIR = obj
BIN_DIR = bin

UNAME_S := $(shell uname -s)

COMMON_SRCS = $(wildcard src/common/*.c)
POSIX_SRCS = $(wildcard src/platform/posix_*.c)
MAC_SRCS = $(wildcard src/platform/mac_*.c) $(POSIX_SRCS)
LINUX_SRCS = $(wildcard src/platform/linux_*.c) $(POSIX_SRCS)
WIN32_SRCS = $(wildcard src/platform/win32_*.c)

ifeq ($(UNAME_S),Darwin)
PLATFORM_SRCS = $(MAC_SRCS)
else ifeq ($(UNAME_S),Linux)
PLATFORM_SRCS = $(LINUX_SRCS)
else ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
PLATFORM_SRCS = $(WIN32_SRCS)
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
PLATFORM_SRCS = $(WIN32_SRCS)
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
PLATFORM_SRCS = $(WIN32_SRCS)
else
PLATFORM_SRCS = $(POSIX_SRCS)
endif

# Shared source and object files
SHARED_SRCS = $(COMMON_SRCS) $(PLATFORM_SRCS)
SHARED_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SHARED_SRCS))

# Targets
SERVER_TARGET = $(BIN_DIR)/server
CLIENT_TARGET = $(BIN_DIR)/client

.PHONY: all clean

all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Build Server
$(SERVER_TARGET): $(SHARED_OBJS) $(OBJ_DIR)/server/main.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build Client
$(CLIENT_TARGET): $(SHARED_OBJS) $(OBJ_DIR)/client/main.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile C source files into Object files
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)