#include "net.h"
#include "protocol.h"
#include "logging.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

// ---- Color pairs (constants live in client.h) ---------------------------

static const short NICK_COLORS[] = {
    COLOR_CYAN, COLOR_GREEN, COLOR_MAGENTA, COLOR_YELLOW,
    COLOR_RED, COLOR_BLUE, COLOR_WHITE};
#define NICK_PALETTE_N ((int)(sizeof(NICK_COLORS) / sizeof(NICK_COLORS[0])))

// Deterministic per-nick color pair so a name always looks the same.
static int nick_pair(const char *nick)
{
    unsigned h = 0;
    for (const char *p = nick; *p; p++)
        h = h * 31u + (unsigned char)*p;
    return PAIR_NICK0 + (int)(h % NICK_PALETTE_N);
}

static void now_hm(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm)
        strftime(buf, n, "%H:%M", tm);
    else
        snprintf(buf, n, "--:--");
}

void ui_colors(void)
{
    if (!has_colors())
        return;
    start_color();
    use_default_colors();
    init_pair(PAIR_BAR, COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_SYS, COLOR_YELLOW, -1);
    init_pair(PAIR_TIME, COLOR_CYAN, -1);
    init_pair(PAIR_HDR, COLOR_WHITE, -1);
    init_pair(PAIR_BORDER, COLOR_BLUE, -1);
    init_pair(PAIR_LOGO, COLOR_MAGENTA, -1);
    init_pair(PAIR_LABEL, COLOR_GREEN, -1);
    init_pair(PAIR_ACCENT, COLOR_CYAN, -1);
    init_pair(PAIR_ERR, COLOR_RED, -1);
    init_pair(PAIR_INFO, COLOR_GREEN, -1);
    for (int i = 0; i < NICK_PALETTE_N; i++)
        init_pair(PAIR_NICK0 + i, NICK_COLORS[i], -1);
}

// ---- Chrome (all assume the caller holds ui_lock) -----------------------

static void draw_top(client_ctx_t *ctx)
{
    int cols = getmaxx(ctx->top_bar);
    werase(ctx->top_bar);

    wattron(ctx->top_bar, A_BOLD);
    mvwprintw(ctx->top_bar, 0, 1, "ESPRA");
    wattroff(ctx->top_bar, A_BOLD);
    wprintw(ctx->top_bar, "  •  %s  •  #%s", ctx->server_ip, ctx->channel);

    const char *hint = "^C quit ";
    int hx = cols - (int)strlen(hint);
    if (hx > 0)
        mvwprintw(ctx->top_bar, 0, hx, "%s", hint);

    wnoutrefresh(ctx->top_bar);
}

static void draw_status(client_ctx_t *ctx)
{
    char hm[8];
    now_hm(hm, sizeof(hm));
    werase(ctx->status_bar);
    mvwprintw(ctx->status_bar, 0, 1,
              "[%s] [%s] connected to %s - %d user%s",
              hm, ctx->name, ctx->server_ip,
              ctx->nick_count, ctx->nick_count == 1 ? "" : "s");
    wnoutrefresh(ctx->status_bar);
}

// Colored border + title common to the framed panels.
static void draw_frame(WINDOW *frame, const char *title)
{
    wattron(frame, COLOR_PAIR(PAIR_BORDER));
    box(frame, 0, 0);
    wattroff(frame, COLOR_PAIR(PAIR_BORDER));
    wattron(frame, COLOR_PAIR(PAIR_HDR) | A_BOLD);
    mvwprintw(frame, 0, 2, " %s ", title);
    wattroff(frame, COLOR_PAIR(PAIR_HDR) | A_BOLD);
    wnoutrefresh(frame);
}

static void draw_nicks(client_ctx_t *ctx)
{
    if (!ctx->nick_win)
        return;
    int rows, cols;
    getmaxyx(ctx->nick_win, rows, cols);

    char title[32];
    snprintf(title, sizeof(title), "Users (%d)", ctx->nick_count);
    draw_frame(ctx->nick_frame, title);

    werase(ctx->nick_win);
    for (int i = 0; i < ctx->nick_count && i < rows; i++)
    {
        bool self = (strncmp(ctx->nicks[i], ctx->name, ESPRA_NAME_MAX) == 0);
        int pair = nick_pair(ctx->nicks[i]);
        int attr = COLOR_PAIR(pair) | (self ? A_BOLD : 0);
        wattron(ctx->nick_win, attr);
        mvwprintw(ctx->nick_win, i, 0, "%s%.*s",
                  self ? "* " : "  ", cols - 2, ctx->nicks[i]);
        wattroff(ctx->nick_win, attr);
    }
    wnoutrefresh(ctx->nick_win);
}

void ui_render_input(client_ctx_t *ctx)
{
    WINDOW *win = ctx->input_win;
    int rows, cols;
    getmaxyx(win, rows, cols);
    (void)rows;

    werase(win);

    // "[nick] " prompt in the nick's own color.
    int pair = nick_pair(ctx->name);
    wattron(win, COLOR_PAIR(pair) | A_BOLD);
    mvwprintw(win, 0, 0, "[%s]", ctx->name);
    wattroff(win, COLOR_PAIR(pair) | A_BOLD);
    int prompt_w = (int)strlen(ctx->name) + 3; // "[", "]", " "
    mvwprintw(win, 0, prompt_w - 1, " ");

    int avail = cols - prompt_w - 1;
    if (avail < 1)
        avail = 1;

    const char *view = ctx->input_buf;
    int len = ctx->input_len;
    if (len > avail)
    {
        view += (len - avail);
        len = avail;
    }

    mvwprintw(win, 0, prompt_w, "%.*s", len, view);
    wmove(win, 0, prompt_w + len);
    wnoutrefresh(win);
}

void ui_init(client_ctx_t *ctx)
{
    int body_h = LINES - 3; // top bar + status bar + input line
    if (body_h < 3)
        body_h = 3;
    int panel_w = NICK_PANEL_W;
    if (panel_w > COLS - 24)
        panel_w = (COLS > 40) ? COLS / 4 : 0;
    int chat_fw = COLS - panel_w;
    if (chat_fw < 4)
        chat_fw = COLS;

    ctx->top_bar = newwin(1, COLS, 0, 0);
    ctx->chat_frame = newwin(body_h, chat_fw, 1, 0);
    ctx->nick_frame = panel_w ? newwin(body_h, panel_w, 1, chat_fw) : NULL;
    ctx->status_bar = newwin(1, COLS, LINES - 2, 0);
    ctx->input_win = newwin(1, COLS, LINES - 1, 0);

    // Scrolling text lives inside the bordered frames.
    ctx->chat_win = derwin(ctx->chat_frame, body_h - 2, chat_fw - 2, 1, 1);
    ctx->nick_win = ctx->nick_frame
                        ? derwin(ctx->nick_frame, body_h - 2, panel_w - 2, 1, 1)
                        : NULL;

    scrollok(ctx->chat_win, TRUE);
    keypad(ctx->input_win, TRUE);

    wbkgd(ctx->top_bar, COLOR_PAIR(PAIR_BAR));
    wbkgd(ctx->status_bar, COLOR_PAIR(PAIR_BAR));

    char chan_title[ESPRA_CHANNEL_MAX + 2];
    snprintf(chan_title, sizeof(chan_title), "#%s", ctx->channel);

    draw_top(ctx);
    draw_frame(ctx->chat_frame, chan_title);
    draw_status(ctx);
    if (ctx->nick_win)
        draw_nicks(ctx);
    ui_render_input(ctx);
    doupdate();
}

// ---- Message output -----------------------------------------------------

void ui_msg(client_ctx_t *ctx, const char *nick, const char *text)
{
    char hm[8];
    now_hm(hm, sizeof(hm));

    espra_mutex_lock(ctx->ui_lock);
    WINDOW *w = ctx->chat_win;
    wattron(w, COLOR_PAIR(PAIR_TIME));
    wprintw(w, "%s ", hm);
    wattroff(w, COLOR_PAIR(PAIR_TIME));

    int pair = nick_pair(nick);
    wattron(w, COLOR_PAIR(pair) | A_BOLD);
    wprintw(w, "<%s>", nick);
    wattroff(w, COLOR_PAIR(pair) | A_BOLD);

    wprintw(w, " %s\n", text);
    wnoutrefresh(w);
    ui_render_input(ctx);
    doupdate();
    espra_mutex_unlock(ctx->ui_lock);
}

// Shared body for the timestamped notice lines (system / error / info):
// "HH:MM <tag> text" with the tag+text painted in `pair`. Takes ui_lock.
static void ui_notice(client_ctx_t *ctx, int pair, const char *tag,
                      const char *text)
{
    char hm[8];
    now_hm(hm, sizeof(hm));

    espra_mutex_lock(ctx->ui_lock);
    WINDOW *w = ctx->chat_win;
    wattron(w, COLOR_PAIR(PAIR_TIME));
    wprintw(w, "%s ", hm);
    wattroff(w, COLOR_PAIR(PAIR_TIME));

    wattron(w, COLOR_PAIR(pair));
    wprintw(w, "%s %s\n", tag, text);
    wattroff(w, COLOR_PAIR(pair));

    wnoutrefresh(w);
    ui_render_input(ctx);
    doupdate();
    espra_mutex_unlock(ctx->ui_lock);
}

void ui_system(client_ctx_t *ctx, const char *text)
{
    ui_notice(ctx, PAIR_SYS, "-!-", text);
}

void ui_error(client_ctx_t *ctx, const char *text)
{
    ui_notice(ctx, PAIR_ERR, "[ERROR]", text);
}

void ui_info(client_ctx_t *ctx, const char *text)
{
    ui_notice(ctx, PAIR_INFO, "[INFO]", text);
}

void ui_sys(client_ctx_t *ctx, const char *text)
{
    ui_notice(ctx, PAIR_SYS, "[SYS]", text);
}

void ui_tick(client_ctx_t *ctx)
{
    static int last_min = -1;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    int cur = tm ? tm->tm_min : -1;
    if (cur == last_min)
        return;
    last_min = cur;

    espra_mutex_lock(ctx->ui_lock);
    draw_status(ctx);
    ui_render_input(ctx);
    doupdate();
    espra_mutex_unlock(ctx->ui_lock);
}

// ---- Roster -------------------------------------------------------------

static int nick_index(client_ctx_t *ctx, const char *nick)
{
    for (int i = 0; i < ctx->nick_count; i++)
        if (strncmp(ctx->nicks[i], nick, ESPRA_NAME_MAX) == 0)
            return i;
    return -1;
}

void ui_nick_add(client_ctx_t *ctx, const char *nick)
{
    if (nick[0] == '\0')
        return;
    espra_mutex_lock(ctx->ui_lock);
    if (nick_index(ctx, nick) < 0 && ctx->nick_count < ESPRA_MAX_NICKS)
    {
        strncpy(ctx->nicks[ctx->nick_count], nick, ESPRA_NAME_MAX - 1);
        ctx->nicks[ctx->nick_count][ESPRA_NAME_MAX - 1] = '\0';
        ctx->nick_count++;
        if (ctx->nick_win)
            draw_nicks(ctx);
        draw_status(ctx);
        ui_render_input(ctx);
        doupdate();
    }
    espra_mutex_unlock(ctx->ui_lock);
}

void ui_nick_remove(client_ctx_t *ctx, const char *nick)
{
    espra_mutex_lock(ctx->ui_lock);
    int idx = nick_index(ctx, nick);
    if (idx >= 0)
    {
        for (int i = idx; i < ctx->nick_count - 1; i++)
            memcpy(ctx->nicks[i], ctx->nicks[i + 1], ESPRA_NAME_MAX);
        ctx->nick_count--;
        if (ctx->nick_win)
            draw_nicks(ctx);
        draw_status(ctx);
        ui_render_input(ctx);
        doupdate();
    }
    espra_mutex_unlock(ctx->ui_lock);
}

void ui_set_channel(client_ctx_t *ctx, const char *channel)
{
    if (channel[0] == '\0')
        return;
    espra_mutex_lock(ctx->ui_lock);

    strncpy(ctx->channel, channel, ESPRA_CHANNEL_MAX - 1);
    ctx->channel[ESPRA_CHANNEL_MAX - 1] = '\0';

    // Roster is per-channel; reset it to just us and re-learn the rest from
    // traffic in the new channel.
    ctx->nick_count = 0;
    strncpy(ctx->nicks[0], ctx->name, ESPRA_NAME_MAX - 1);
    ctx->nicks[0][ESPRA_NAME_MAX - 1] = '\0';
    ctx->nick_count = 1;

    char chan_title[ESPRA_CHANNEL_MAX + 2];
    snprintf(chan_title, sizeof(chan_title), "#%s", ctx->channel);

    draw_top(ctx);
    draw_frame(ctx->chat_frame, chan_title);
    if (ctx->nick_win)
        draw_nicks(ctx);
    draw_status(ctx);
    ui_render_input(ctx);
    doupdate();

    espra_mutex_unlock(ctx->ui_lock);
}

void ui_set_nick(client_ctx_t *ctx, const char *newname)
{
    if (newname[0] == '\0')
        return;
    espra_mutex_lock(ctx->ui_lock);

    // Swap our entry in the roster, if present.
    int idx = nick_index(ctx, ctx->name);
    strncpy(ctx->name, newname, ESPRA_NAME_MAX - 1);
    ctx->name[ESPRA_NAME_MAX - 1] = '\0';
    if (idx >= 0)
    {
        strncpy(ctx->nicks[idx], ctx->name, ESPRA_NAME_MAX - 1);
        ctx->nicks[idx][ESPRA_NAME_MAX - 1] = '\0';
    }

    if (ctx->nick_win)
        draw_nicks(ctx);
    draw_status(ctx);
    ui_render_input(ctx);
    doupdate();

    espra_mutex_unlock(ctx->ui_lock);
}


void *listener_thread(void *arg)
{
    client_ctx_t *ctx = (client_ctx_t *)arg;
    char reply_buf[ESPRA_MSG_MAX];
    espra_header_t serv_hdr;

    while (1)
    {
        packet_status_t status = packet_read(ctx->sock, &serv_hdr, reply_buf, ESPRA_MSG_MAX);
        if (status != PACKET_OK)
        {
            ctx->connected = 0;
            ui_system(ctx, "Connection lost. Press any key to exit.");
            return NULL;
        }

        size_t payload_len = serv_hdr.packet_len - sizeof(espra_header_t);
        if (payload_len < ESPRA_MSG_MAX)
            reply_buf[payload_len] = '\0';
        else
            reply_buf[ESPRA_MSG_MAX - 1] = '\0';

        switch (serv_hdr.command)
        {
        case ESPRA_CMD_BCAST: {
            char nickname[ESPRA_NAME_MAX] = {0};
            strncpy(nickname, reply_buf, ESPRA_NAME_MAX - 1);
            nickname[ESPRA_NAME_MAX - 1] = '\0';
            ui_nick_add(ctx, nickname);
            ui_msg(ctx, nickname, reply_buf + ESPRA_NAME_MAX);
        } break;
        case ESPRA_CMD_ERR:
            ui_error(ctx, reply_buf);
            break;
        case ESPRA_CMD_INFO:
            ui_info(ctx, reply_buf);
            break;
        case ESPRA_CMD_SYS:
            ui_sys(ctx, reply_buf);
            break;
        case ESPRA_CMD_DMSG:
        {
            char sender[ESPRA_NAME_MAX] = {0};
            strncpy(sender, reply_buf, ESPRA_NAME_MAX - 1);
            sender[ESPRA_NAME_MAX - 1] = '\0';
            char whisper[ESPRA_MSG_MAX];
            snprintf(whisper, sizeof(whisper), "(whispers) %s", reply_buf + ESPRA_NAME_MAX);
            ui_msg(ctx, sender, whisper);
        } break;
        case ESPRA_CMD_JOIN:
        {
            // Server-authoritative channel switch (echoed back after a /join).
            ui_set_channel(ctx, reply_buf);
            char note[ESPRA_CHANNEL_MAX + 24];
            snprintf(note, sizeof(note), "Now talking in #%.*s", ESPRA_CHANNEL_MAX - 1, reply_buf);
            ui_info(ctx, note);
        } break;
        case ESPRA_CMD_NICK:
        {
            // Server confirmed our own rename.
            ui_set_nick(ctx, reply_buf);
            char note[ESPRA_NAME_MAX + 24];
            snprintf(note, sizeof(note), "You are now known as %.*s", ESPRA_NAME_MAX - 1, reply_buf);
            ui_info(ctx, note);
        } break;
        default:
            break;
        }
    }
    return NULL;
}
