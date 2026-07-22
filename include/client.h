#pragma once

#include "net.h"
#include "thread.h"
#include "protocol.h"

#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

#define ESPRA_MSG_MAX 1024
#define ESPRA_MAX_NICKS 32 // mirrors the server's MAX_CLIENTS
#define NICK_PANEL_W 20    // width of the right-hand user list
#define ESPRA_MAX_CMD 64

// ---- Color pairs (initialized by ui_colors) -----------------------------
#define PAIR_BAR 1    // title / status bars
#define PAIR_SYS 2    // system notices
#define PAIR_TIME 3   // timestamps
#define PAIR_HDR 4    // panel / frame titles
#define PAIR_BORDER 5 // window borders
#define PAIR_LOGO 6   // login banner
#define PAIR_LABEL 7  // login field labels
#define PAIR_ACCENT 8 // misc highlights
#define PAIR_ERR 9    // error notices
#define PAIR_INFO 10  // info notices
#define PAIR_NICK0 11 // first of the rotating nick palette

/**
 * Shared client UI state.
 *
 * ncurses keeps a single global virtual screen, so it is not thread-safe.
 * Every access to the windows below MUST be made while holding ui_lock, which
 * serializes the input loop (main thread) against the network listener thread.
 */
typedef struct
{
    net_socket_t sock;
    espra_mutex_t *ui_lock; // guards all ncurses calls + the nick list

    // Chrome, from top to bottom.
    WINDOW *top_bar;    // title bar
    WINDOW *chat_frame; // border + "#global" title around the chat
    WINDOW *chat_win;   // scrolling message history (inside chat_frame)
    WINDOW *nick_frame; // border + "Users (N)" title around the roster
    WINDOW *nick_win;   // user list (inside nick_frame)
    WINDOW *status_bar; // clock / nick / user count
    WINDOW *input_win;  // single-line editor

    char name[ESPRA_NAME_MAX];       // this client's display name
    char channel[ESPRA_CHANNEL_MAX]; // channel we are currently in
    char server_ip[64];              // shown in the status bar

    // Client-side roster, inferred from join/leave/message traffic.
    char nicks[ESPRA_MAX_NICKS][ESPRA_NAME_MAX];
    int nick_count;

    char input_buf[ESPRA_MSG_MAX]; // current line being typed
    int input_len;

    volatile int connected; // cleared by the listener when the server drops
} client_ctx_t;

// ---- UI (all take ui_lock internally unless noted) ----------------------

// Initializes the shared color palette. Call once, right after initscr(),
// before drawing the login window or the chat UI. Safe if the terminal has
// no color support.
void ui_colors(void);

// Builds/sizes every window and paints the initial chrome.
void ui_init(client_ctx_t *ctx);

// Appends a chat line "HH:MM <nick> text" with a per-nick color.
void ui_msg(client_ctx_t *ctx, const char *nick, const char *text);

// Appends a system notice "HH:MM -!- text" in the system color.
void ui_system(client_ctx_t *ctx, const char *text);

// Appends an error notice "HH:MM [ERROR] text" in the error color.
void ui_error(client_ctx_t *ctx, const char *text);

// Appends an info notice "HH:MM [INFO] text" in the info color.
void ui_info(client_ctx_t *ctx, const char *text);

// Appends a server notice "HH:MM [SYS] text" in the system color.
void ui_sys(client_ctx_t *ctx, const char *text);

// Redraws the input line from ctx->input_buf and parks the cursor.
// Caller MUST already hold ui_lock.
void ui_render_input(client_ctx_t *ctx);

// Refreshes the clock in the status bar if the minute changed. Called from the
// idle input loop. Takes ui_lock internally.
void ui_tick(client_ctx_t *ctx);

// ---- Roster -------------------------------------------------------------

// Adds/removes a nick and repaints the panel + status bar. Take ui_lock.
void ui_nick_add(client_ctx_t *ctx, const char *nick);
void ui_nick_remove(client_ctx_t *ctx, const char *nick);

// Switches the displayed channel: repaints the chrome and resets the roster to
// just this client (channel membership is re-learned from traffic). Take ui_lock.
void ui_set_channel(client_ctx_t *ctx, const char *channel);

// Applies a confirmed nick change: updates ctx->name, the roster and the
// prompt. Take ui_lock.
void ui_set_nick(client_ctx_t *ctx, const char *newname);

// Background network reader. arg is a client_ctx_t*.
void *listener_thread(void *arg);
