# ESPRA build.
#
# Native build (host OS auto-detected):
#     make
# Cross-compile for Windows (needs mingw-w64: x86_64-w64-mingw32-gcc):
#     make TARGET=windows
#
# Objects are kept per-target under obj/<target>/ so native and cross builds do
# not clobber each other. Binaries land in bin/ (Windows ones get a .exe suffix).

UNAME_S := $(shell uname -s)

# Canonical name of the host, used as the default TARGET.
ifeq ($(UNAME_S),Darwin)
HOST_TARGET = macos
else ifeq ($(UNAME_S),Linux)
HOST_TARGET = linux
else ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
HOST_TARGET = windows
else ifeq ($(findstring MSYS,$(UNAME_S)),MSYS)
HOST_TARGET = windows
else ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
HOST_TARGET = windows
else
HOST_TARGET = posix
endif

TARGET ?= $(HOST_TARGET)

CFLAGS = -Wall -Wextra -Wpedantic -std=c99 -Iinclude
LDFLAGS =

COMMON_SRCS = $(wildcard src/common/*.c)
POSIX_SRCS  = $(wildcard src/platform/posix_*.c)
MAC_SRCS    = $(wildcard src/platform/mac_*.c) $(POSIX_SRCS)
LINUX_SRCS  = $(wildcard src/platform/linux_*.c) $(POSIX_SRCS)
WIN32_SRCS  = $(wildcard src/platform/win32_*.c)

# PDCurses (the client's curses backend on Windows) is vendored/built on demand.
PDCURSES_DIR = deps/PDCurses
PDCURSES_LIB = $(PDCURSES_DIR)/libpdcurses.a
PDCURSES_URL = https://github.com/wmcbrine/PDCurses.git

# ---- Per-target toolchain & flags ---------------------------------------
EXE =
CLIENT_DEPS =

ifeq ($(TARGET),windows)
CROSS       ?= x86_64-w64-mingw32-
CC          := $(CROSS)gcc
AR          := $(CROSS)ar
PLATFORM_SRCS = $(WIN32_SRCS)
LDFLAGS     += -lws2_32
CFLAGS      += -I$(PDCURSES_DIR)
CLIENT_LIBS  = -L$(PDCURSES_DIR) -lpdcurses
CLIENT_DEPS  = $(PDCURSES_LIB)
EXE          = .exe
else ifeq ($(TARGET),macos)
CC          ?= cc
PLATFORM_SRCS = $(MAC_SRCS)
CLIENT_LIBS  = -lncurses
else ifeq ($(TARGET),linux)
CC          ?= cc
PLATFORM_SRCS = $(LINUX_SRCS)
CLIENT_LIBS  = -lncurses
else
CC          ?= cc
PLATFORM_SRCS = $(POSIX_SRCS)
CLIENT_LIBS  = -lncurses
endif

OBJ_DIR = obj/$(TARGET)
BIN_DIR = bin

# Shared source and object files
SHARED_SRCS = $(COMMON_SRCS) $(PLATFORM_SRCS)
SHARED_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SHARED_SRCS))
CLIENT_SRCS = $(wildcard src/client/*.c)
CLIENT_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(CLIENT_SRCS))

SERVER_TARGET = $(BIN_DIR)/server$(EXE)
CLIENT_TARGET = $(BIN_DIR)/client$(EXE)

.PHONY: all clean pdcurses

all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Build Server
$(SERVER_TARGET): $(SHARED_OBJS) $(OBJ_DIR)/server/main.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build Client (depends on PDCurses only when cross-compiling for Windows)
$(CLIENT_TARGET): $(SHARED_OBJS) $(CLIENT_OBJS) $(CLIENT_DEPS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SHARED_OBJS) $(CLIENT_OBJS) -o $@ $(LDFLAGS) $(CLIENT_LIBS)

# Compile C source files into Object files
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- PDCurses (Windows client backend) ----------------------------------
# Fetch the sources on first use, then build a static lib with the same
# toolchain as the rest of the Windows build.
pdcurses: $(PDCURSES_LIB)

$(PDCURSES_DIR):
	git clone --depth 1 $(PDCURSES_URL) $@

$(PDCURSES_LIB): | $(PDCURSES_DIR)
	@echo "Building PDCurses with $(CC)..."
	@mkdir -p $(PDCURSES_DIR)/obj
	@for f in $(PDCURSES_DIR)/pdcurses/*.c $(PDCURSES_DIR)/wincon/*.c; do \
		$(CC) -O2 -I$(PDCURSES_DIR) -I$(PDCURSES_DIR)/wincon \
			-c $$f -o $(PDCURSES_DIR)/obj/`basename $${f%.c}`.o || exit 1; \
	done
	$(AR) rcs $@ $(PDCURSES_DIR)/obj/*.o

clean:
	rm -rf obj bin
	rm -rf $(PDCURSES_DIR)/obj $(PDCURSES_LIB)
