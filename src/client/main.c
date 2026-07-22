#include "net.h"
#include "logging.h"
#include "thread.h"
#include "client.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>

#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

#define INPUT_POLL_MS 60 // getch timeout so the loop can watch the clock/connection

static void close_window(int sig)
{
    (void)sig;
    endwin();
    exit(0);
}

// Small ASCII banner shown in the login window.
static const char *LOGO[] = {
    " _____ ____  ____  ____      _    ",
    "| ____/ ___||  _ \\|  _ \\    / \\   ",
    "|  _| \\___ \\| |_) | |_) |  / _ \\  ",
    "| |___ ___) |  __/|  _ <  / ___ \\ ",
    "|_____|____/|_|   |_| \\_\\/_/   \\_\\",
};
#define LOGO_LINES ((int)(sizeof(LOGO) / sizeof(LOGO[0])))
#define LOGO_W 34

#define LOGIN_H 15
#define LOGIN_W 56

// Clears just the interior of a boxed row (keeps the borders intact).
static void login_clear_row(WINDOW *w, int y)
{
    mvwhline(w, y, 2, ' ', getmaxx(w) - 4);
}

// Colored status line at the bottom of the login window.
static void login_status(WINDOW *w, const char *msg, int pair)
{
    login_clear_row(w, LOGIN_H - 3);
    wattron(w, COLOR_PAIR(pair) | A_BOLD);
    mvwprintw(w, LOGIN_H - 3, 6, "%s", msg);
    wattroff(w, COLOR_PAIR(pair) | A_BOLD);
    wrefresh(w);
}

// Draws a labeled, underlined field and reads a line into buf.
static void login_field(WINDOW *w, int y, const char *label, char *buf, size_t max)
{
    wattron(w, COLOR_PAIR(PAIR_LABEL) | A_BOLD);
    mvwprintw(w, y, 6, "%s", label);
    wattroff(w, COLOR_PAIR(PAIR_LABEL) | A_BOLD);

    int fx = 6 + (int)strlen(label);
    int fw = getmaxx(w) - fx - 4;
    if (fw < 1)
        fw = 1;

    wattron(w, A_UNDERLINE);
    mvwhline(w, y, fx, ' ', fw);
    wmove(w, y, fx);
    echo();
    curs_set(1);
    int n = (int)max - 1;
    if (n > fw)
        n = fw;
    wgetnstr(w, buf, n);
    noecho();
    curs_set(0);
    wattroff(w, A_UNDERLINE);
    buf[strcspn(buf, "\n")] = '\0';
}

// Builds the centered login window and paints its static chrome.
static WINDOW *login_window(void)
{
    int sy = (LINES - LOGIN_H) / 2;
    if (sy < 0)
        sy = 0;
    int sx = (COLS - LOGIN_W) / 2;
    if (sx < 0)
        sx = 0;
    WINDOW *w = newwin(LOGIN_H, LOGIN_W, sy, sx);
    keypad(w, TRUE);

    wattron(w, COLOR_PAIR(PAIR_BORDER));
    box(w, 0, 0);
    wattroff(w, COLOR_PAIR(PAIR_BORDER));
    wattron(w, COLOR_PAIR(PAIR_HDR) | A_BOLD);
    mvwprintw(w, 0, 2, " connect ");
    wattroff(w, COLOR_PAIR(PAIR_HDR) | A_BOLD);

    int lx = 1 + (LOGIN_W - 2 - LOGO_W) / 2;
    if (lx < 1)
        lx = 1;
    wattron(w, COLOR_PAIR(PAIR_LOGO) | A_BOLD);
    for (int i = 0; i < LOGO_LINES; i++)
        mvwprintw(w, 2 + i, lx, "%s", LOGO[i]);
    wattroff(w, COLOR_PAIR(PAIR_LOGO) | A_BOLD);

    const char *tag = "a tiny terminal chat";
    wattron(w, COLOR_PAIR(PAIR_ACCENT));
    mvwprintw(w, 2 + LOGO_LINES, (LOGIN_W - (int)strlen(tag)) / 2, "%s", tag);
    wattroff(w, COLOR_PAIR(PAIR_ACCENT));

    wrefresh(w);
    return w;
}

// Parses and dispatches a slash-command typed at the input line. `outgoing`
// is mutated in place while splitting off arguments.
static void parse_command(client_ctx_t *ctx, char *outgoing, net_socket_t sock)
{
    // Split "/cmd rest..." into the command word and its argument tail.
    char *args = strchr(outgoing, ' ');
    if (args)
    {
        *args++ = '\0';
        while (*args == ' ')
            args++;
    }
    else
    {
        args = outgoing + strlen(outgoing); // points at ""
    }

    espra_header_t header = {0};

    if (strcmp(outgoing, "/msg") == 0)
    {
        // args = "<nick> <message...>"
        char *text = strchr(args, ' ');
        if (!text || args[0] == '\0')
        {
            ui_error(ctx, "Usage: /msg <nick> <message>");
            return;
        }
        *text++ = '\0';
        while (*text == ' ')
            text++;
        if (*text == '\0')
        {
            ui_error(ctx, "Usage: /msg <nick> <message>");
            return;
        }

        char payload[ESPRA_NAME_MAX + ESPRA_MSG_MAX];
        memset(payload, 0, ESPRA_NAME_MAX);
        strncpy(payload, args, ESPRA_NAME_MAX - 1); // target name field
        size_t tlen = strnlen(text, ESPRA_MSG_MAX - 1);
        memcpy(payload + ESPRA_NAME_MAX, text, tlen);

        header.command = ESPRA_CMD_DMSG;
        header.packet_len = sizeof(espra_header_t) + ESPRA_NAME_MAX + tlen;
        if (packet_write(sock, &header, payload) != PACKET_OK)
        {
            ctx->connected = 0;
            return;
        }

        char echo[ESPRA_MSG_MAX];
        snprintf(echo, sizeof(echo), "(to %s) %.*s", args, (int)tlen, text);
        ui_info(ctx, echo);
    }
    else if (strcmp(outgoing, "/join") == 0)
    {
        if (args[0] == '#')
            args++; // tolerate a leading '#'
        char *sp = strchr(args, ' ');
        if (sp)
            *sp = '\0'; // channel names are a single token
        if (args[0] == '\0')
        {
            ui_error(ctx, "Usage: /join <channel>");
            return;
        }
        header.command = ESPRA_CMD_JOIN;
        header.packet_len = sizeof(espra_header_t) + strnlen(args, ESPRA_CHANNEL_MAX - 1);
        if (packet_write(sock, &header, args) != PACKET_OK)
            ctx->connected = 0;
        // The UI switches only once the server echoes the JOIN back.
    }
    else if (strcmp(outgoing, "/nick") == 0)
    {
        char *sp = strchr(args, ' ');
        if (sp)
            *sp = '\0';
        if (args[0] == '\0')
        {
            ui_error(ctx, "Usage: /nick <newname>");
            return;
        }
        header.command = ESPRA_CMD_NICK;
        header.packet_len = sizeof(espra_header_t) + strnlen(args, ESPRA_NAME_MAX - 1);
        if (packet_write(sock, &header, args) != PACKET_OK)
            ctx->connected = 0;
        // ctx->name updates only once the server confirms the rename.
    }
    else if (strcmp(outgoing, "/list") == 0)
    {
        header.command = ESPRA_CMD_LIST;
        header.packet_len = sizeof(espra_header_t); // empty request
        if (packet_write(sock, &header, "") != PACKET_OK)
            ctx->connected = 0;
    }
    else if (strcmp(outgoing, "/help") == 0)
    {
        ui_info(ctx, "/msg <nick> <text>  - private message");
        ui_info(ctx, "/join <channel>     - switch channel");
        ui_info(ctx, "/nick <newname>     - change your name");
        ui_info(ctx, "/list               - list channels and users");
        ui_info(ctx, "/help               - show this list");
        ui_info(ctx, "/quit               - disconnect");
    }
    else if (strcmp(outgoing, "/quit") == 0)
    {
        ctx->connected = 0;
    }
    else
    {
        ui_error(ctx, "Unknown command. Try /help");
    }
}

int main(void)
{
    signal(SIGINT, close_window);
    signal(SIGTERM, close_window);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    ui_colors();
    curs_set(0);

    // ---- Connect screen (centered window) -------------------------------
    client_ctx_t ctx = {0};

    erase();
    refresh();
    WINDOW *login = login_window();

    login_field(login, 9, "name   : ", ctx.name, sizeof(ctx.name));
    login_field(login, 10, "server : ", ctx.server_ip, sizeof(ctx.server_ip));

    if (ctx.server_ip[0] == '\0')
    {
        login_status(login, "No server IP given. Press any key to exit.", PAIR_SYS);
        wgetch(login);
        delwin(login);
        endwin();
        return -1;
    }

    net_socket_t sock = net_socket(NET_AF_INET, NET_SOCK_STREAM, 0);
    if (sock < 0)
    {
        delwin(login);
        endwin();
        LOG_ERROR("Failed to create socket.");
        return sock;
    }
    ctx.sock = sock;

    login_status(login, "Connecting...", PAIR_ACCENT);
    if (net_connect(sock, ctx.server_ip, ESPRA_PORT) != 0)
    {
        login_status(login, "Could not connect. Press any key to exit.", PAIR_SYS);
        wgetch(login);
        net_close(sock);
        delwin(login);
        endwin();
        return -1;
    }

    // ---- Authenticate ---------------------------------------------------
    espra_header_t header = {0};
    header.command = ESPRA_CMD_AUTH_REQ;
    header.packet_len = sizeof(espra_header_t) + ESPRA_NAME_MAX + ESPRA_PWD_MAX;

    char payload[ESPRA_NAME_MAX + ESPRA_PWD_MAX];
    memset(payload, 0, sizeof(payload));
    strncpy(payload, ctx.name, ESPRA_NAME_MAX);
    strncpy(payload + ESPRA_NAME_MAX, ESPRA_GLOBAL_PASSWORD, ESPRA_PWD_MAX);
    packet_write(sock, &header, payload);

    espra_header_t serv_hdr;
    char reply_buf[ESPRA_MSG_MAX];
    packet_status_t pstatus = packet_read(sock, &serv_hdr, reply_buf, ESPRA_MSG_MAX);
    if (pstatus != PACKET_OK || serv_hdr.command != ESPRA_CMD_AUTH_RESP)
    {
        login_status(login, "Auth failed (protocol error). Press any key.", PAIR_SYS);
        wgetch(login);
        net_close(sock);
        delwin(login);
        endwin();
        return -1;
    }

    if (serv_hdr.flags & ESPRA_ERROR)
    {
        const char *reason;
        switch ((uint8_t)reply_buf[0])
        {
        case AUTH_ERR_PWD:
            reason = "Wrong server password.";
            break;
        case AUTH_ERR_TAKEN:
            reason = "Username already in use.";
            break;
        case AUTH_ERR_FULL:
            reason = "Server is full (max 32).";
            break;
        default:
            reason = "Unknown auth error.";
            break;
        }
        login_status(login, reason, PAIR_SYS);
        wgetch(login);
        net_close(sock);
        delwin(login);
        endwin();
        return -1;
    }

    delwin(login);

    // ---- Build the chat UI ---------------------------------------------
    ctx.connected = 1;
    strncpy(ctx.channel, ESPRA_DEFAULT_CHANNEL, ESPRA_CHANNEL_MAX - 1);
    ctx.channel[ESPRA_CHANNEL_MAX - 1] = '\0';
    ctx.ui_lock = espra_mutex_create();
    if (ctx.ui_lock == NULL)
    {
        net_close(sock);
        endwin();
        LOG_ERROR("Failed to create UI lock.");
        return -1;
    }

    erase();
    refresh();
    ui_init(&ctx);
    curs_set(1);

    ui_nick_add(&ctx, ctx.name); // we're the first known member
    ui_system(&ctx, "Welcome to ESPRA. Type a message and press Enter, or /help for commands.");

    espra_thread_t *thread = espra_thread_create(listener_thread, &ctx);
    if (thread == NULL)
    {
        espra_mutex_destroy(ctx.ui_lock);
        net_close(sock);
        endwin();
        LOG_ERROR("Failed to start listener thread.");
        return -1;
    }
    espra_thread_detach(thread);
    espra_thread_free(thread);

    wtimeout(ctx.input_win, INPUT_POLL_MS);

    // ---- Input loop -----------------------------------------------------
    while (ctx.connected)
    {
        espra_mutex_lock(ctx.ui_lock);
        int ch = wgetch(ctx.input_win);
        espra_mutex_unlock(ctx.ui_lock);

        if (ch == ERR)
        {
            ui_tick(&ctx); // refresh the clock when idle
            continue;
        }

        if (ch == '\n' || ch == KEY_ENTER || ch == '\r')
        {
            if (ctx.input_len == 0)
                continue;

            char outgoing[ESPRA_MSG_MAX];
            espra_mutex_lock(ctx.ui_lock);
            memcpy(outgoing, ctx.input_buf, ctx.input_len);
            outgoing[ctx.input_len] = '\0';
            ctx.input_len = 0;
            ctx.input_buf[0] = '\0';
            ui_render_input(&ctx);
            doupdate();
            espra_mutex_unlock(ctx.ui_lock);

            if (outgoing[0] == '/') {
                // a command is being sent
                parse_command(&ctx, outgoing, sock);
            } else {

                // Local echo (the server does not send our own messages back).
                ui_msg(&ctx, ctx.name, outgoing);

                espra_header_t msg_hdr = {0};
                msg_hdr.command = ESPRA_CMD_BCAST;
                msg_hdr.packet_len = sizeof(espra_header_t) + strnlen(outgoing, ESPRA_MSG_MAX);
                if (packet_write(sock, &msg_hdr, outgoing) != PACKET_OK)
                {
                    ctx.connected = 0;
                }
            }
        }
        else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8)
        {
            espra_mutex_lock(ctx.ui_lock);
            if (ctx.input_len > 0)
            {
                ctx.input_len--;
                ctx.input_buf[ctx.input_len] = '\0';
                ui_render_input(&ctx);
                doupdate();
            }
            espra_mutex_unlock(ctx.ui_lock);
        }
        else if (isprint(ch) && ctx.input_len < ESPRA_MSG_MAX - 1)
        {
            espra_mutex_lock(ctx.ui_lock);
            ctx.input_buf[ctx.input_len++] = (char)ch;
            ctx.input_buf[ctx.input_len] = '\0';
            ui_render_input(&ctx);
            doupdate();
            espra_mutex_unlock(ctx.ui_lock);
        }
    }

    // Let the user read a disconnect notice before we tear down.
    espra_mutex_lock(ctx.ui_lock);
    wtimeout(ctx.input_win, -1);
    wgetch(ctx.input_win);
    espra_mutex_unlock(ctx.ui_lock);

    net_close(sock);
    espra_mutex_destroy(ctx.ui_lock);
    endwin();
    return 0;
}
