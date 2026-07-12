CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c99 -Iinclude
LDFLAGS = 

OBJ_DIR = obj
BIN_DIR = bin

# Shared source and object files
SHARED_SRCS = $(wildcard src/common/*.c) $(wildcard src/platform/*.c)
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