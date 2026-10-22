/* Player: plays the .mel melodies from the Music folder on the PC speaker.
 * The sequencer runs from the desktop's main loop, so music keeps playing
 * while other windows are used; the Player window and the desktop widget
 * only show and control it. */
#include "gui.h"

static struct player_state st;
static struct note notes[MELODY_MAX];
static int nnotes, cur;
static uint64_t note_start;           /* when the current note began */
static int pos_base;                  /* ms of the notes before `cur` */
static int paused_at;                 /* ms into the current note when paused */
static int speaker_freq;

static char playlist[32][FS_NAME_MAX];
static int nplay;

static void speaker(int freq)
{
    if (freq == speaker_freq)
        return;
    speaker_freq = freq;
    if (freq)
        speaker_on(freq);
    else
        speaker_off();
}

static void scan(void)
{
    nplay = 0;
    for (int i = 0; i < FS_MAX_FILES && nplay < 32; i++) {
        struct file *f = fs_at(i);
        if (f && !f->trashed && fs_folder_of(f->name) == FOLDER_MUSIC && has_ext(f->name, ".mel"))
            str_copy(playlist[nplay++], f->name, FS_NAME_MAX);
    }
    for (int i = 1; i < nplay; i++)
        for (int j = i; j > 0 && strcmp(playlist[j], playlist[j - 1]) < 0; j--) {
            char t[FS_NAME_MAX];
            memcpy(t, playlist[j], FS_NAME_MAX);
            memcpy(playlist[j], playlist[j - 1], FS_NAME_MAX);
            memcpy(playlist[j - 1], t, FS_NAME_MAX);
        }
    st.tracks = nplay;
    st.track = -1;
    for (int i = 0; i < nplay; i++)
        if (!strcmp(playlist[i], st.path))
            st.track = i;
}

static bool load(const char *path)
{
    struct file *f = fs_find(path);
    if (!f)
        return false;
    nnotes = melody_parse(f->data, notes, MELODY_MAX, st.subtitle, sizeof st.subtitle);
    if (!st.subtitle[0])
        str_copy(st.subtitle, "Мелодия", sizeof st.subtitle);
    str_copy(st.path, path, sizeof st.path);
    str_copy(st.title, fs_base(path), sizeof st.title);
    char *dot = strstr(st.title, ".mel");
    if (dot)
        *dot = 0;
    st.len_ms = 0;
    for (int i = 0; i < nnotes; i++)
        st.len_ms += notes[i].ms;
    st.pos_ms = 0;
    cur = 0;
    pos_base = 0;
    paused_at = 0;
    note_start = timer_ms();
    st.loaded = nnotes > 0;
    scan();
    return st.loaded;
}

const struct player_state *player_get(void)
{
    return &st;
}

void player_open(const char *path)
{
    if (load(path)) {
        st.playing = true;
        note_start = timer_ms();
    }
}

void player_toggle(void)
{
    if (!st.loaded) {
        scan();
        if (!nplay || !load(playlist[0]))
            return;
    }
    if (st.playing) {
        st.playing = false;
        paused_at = (int)(timer_ms() - note_start);
        speaker(0);
    } else {
        st.playing = true;
        note_start = timer_ms() - paused_at;
    }
}

void player_skip(int dir)
{
    scan();
    if (!nplay)
        return;
    bool was = st.playing || !st.loaded;
    int t = st.track < 0 ? 0 : (st.track + dir + nplay) % nplay;
    if (load(playlist[t])) {
        st.playing = was;
        note_start = timer_ms();
        if (!was)
            speaker(0);
    }
}

void player_tick(uint64_t now)
{
    if (!st.playing)
        return;
    if ((int64_t)(now - note_start) < 0)
        now = note_start;                 /* started after this loop pass began */
    while (cur < nnotes && now - note_start >= notes[cur].ms) {
        note_start += notes[cur].ms;
        pos_base += notes[cur].ms;
        cur++;
    }
    if (cur >= nnotes) {
        /* next track, or stop after the last one */
        speaker(0);
        int next = st.track + 1;
        scan();
        if (next < nplay && load(playlist[next])) {
            st.playing = true;
            note_start = now;
        } else {
            st.playing = false;
            cur = 0;
            pos_base = 0;
            paused_at = 0;
            st.pos_ms = 0;
            st.freq = 0;
        }
        return;
    }
    int in = (int)(now - note_start);
    int f = notes[cur].freq && in < notes[cur].ms * 88 / 100 ? notes[cur].freq : 0;
    speaker(f);
    st.freq = f;
    st.pos_ms = pos_base + in;
}

/* ---- the window ---- */

#define LIST_W 230

struct pl {
    struct rect rows[32], b_prev, b_play, b_next, bar;
    int hover;
    uint64_t last;
};

static void fmt(int ms, char *b, size_t n)
{
    snprintf(b, n, "%d:%02d", ms / 60000, ms / 1000 % 60);
}

static int pitch_y(int freq, int top, int h)
{
    /* 130 Hz .. 2100 Hz on a log scale */
    double v = (f_log(f_clamp(freq, 130, 2100)) - f_log(130)) / (f_log(2100) - f_log(130));
    return top + h - 8 - (int)(v * (h - 16));
}

static void player_draw(struct window *w, struct canvas *c)
{
    struct pl *p = w->data;
    scan();
    int H = w->h, W = w->w, top = w->title_h;
    /* playlist */
    fill_rect(c, 0, top, LIST_W, H - top, OPAQUE(pal.sidebar_bg));
    fill_rrect_px(c, LIST_W * c->s - 1, top * c->s, 1, (H - top) * c->s, 0, OPAQUE(pal.separator));
    draw_text(c, FONT_UI11, 18, top + 14, "МУЗЫКА", OPAQUE(pal.text3));
    int y = top + 34;
    for (int i = 0; i < nplay && i < 32; i++) {
        struct rect r = { 8, y, LIST_W - 16, 32 };
        p->rows[i] = r;
        bool on = i == st.track;
        if (on)
            fill_rrect(c, r.x, r.y, r.w, r.h, 6, with_alpha(pal.accent, 45));
        else if (i == p->hover)
            fill_rrect(c, r.x, r.y, r.w, r.h, 6, OPAQUE(pal.selection));
        draw_icon(c, ICON_AUDIO_FILE, r.x + 8, r.y + 6, 20, 255);
        char t[FS_NAME_MAX];
        str_copy(t, fs_base(playlist[i]), sizeof t);
        char *dot = strstr(t, ".mel");
        if (dot)
            *dot = 0;
        draw_text_fit(c, on ? FONT_UI13B : FONT_UI13, r.x + 36, r.y + (32 - font_height(FONT_UI13)) / 2, r.w - 60, t,
                      OPAQUE(pal.text));
        if (on && st.playing)
            for (int b = 0; b < 3; b++) {
                int bh = 4 + (int)((f_sin(timer_ms() / 120.0 + b * 1.9) + 1) * 4);
                fill_rect(c, r.x + r.w - 22 + b * 5, r.y + 22 - bh, 3, bh, OPAQUE(pal.accent));
            }
        y += 34;
    }
    if (!nplay)
        draw_text_wrapped(c, FONT_UI12, 18, top + 40, LIST_W - 36,
                          "В папке «Музыка» нет мелодий (.mel). Их можно написать в «Тексте»: E5/4 D5/8 ...",
                          OPAQUE(pal.text2), false);

    /* now playing */
    int x0 = LIST_W + 24, rw = W - x0 - 24;
    draw_icon(c, ICON_PLAYER, x0 - 6, top + 16, 124, 255);
    draw_text_fit(c, FONT_UI20B, x0 + 132, top + 34, rw - 140, st.loaded ? st.title : "Ничего не играет",
                  OPAQUE(pal.text));
    draw_text_fit(c, FONT_UI13, x0 + 132, top + 64, rw - 140, st.loaded ? st.subtitle : "Выбери мелодию слева",
                  OPAQUE(pal.text2));
    if (st.loaded) {
        char info[64];
        fmt(st.len_ms, info, sizeof info);
        int l = strlen(info);
        snprintf(info + l, sizeof info - l, "  ·  %d нот", nnotes);
        draw_text(c, FONT_UI12, x0 + 132, top + 88, info, OPAQUE(pal.text3));
    }
    /* piano roll: the notes around the playhead */
    struct rect roll = { x0, top + 152, rw, MAX(80, H - top - 152 - 120) };
    fill_rrect(c, roll.x, roll.y, roll.w, roll.h, 10, OPAQUE(pal.dark ? 0x141418 : 0x1F2233));
    struct canvas cr = *c;
    cv_clip_pt(&cr, roll.x + 2, roll.y + 2, roll.w - 4, roll.h - 4);
    int head = roll.x + roll.w / 3;
    double px_per_ms = 0.11;
    if (st.loaded) {
        int t = 0;
        for (int i = 0; i < nnotes; i++) {
            int nx = head + (int)((t - st.pos_ms) * px_per_ms);
            int nw = MAX(3, (int)(notes[i].ms * px_per_ms) - 2);
            t += notes[i].ms;
            if (nx > roll.x + roll.w || nx + nw < roll.x || !notes[i].freq)
                continue;
            int ny = pitch_y(notes[i].freq, roll.y, roll.h);
            bool now = i == cur && st.playing;
            uint32_t col = now ? 0xFFFFFFFF : nx + nw < head ? ARGB(255, 0x6A, 0x5C, 0xFF) : ARGB(255, 0xFF, 0x6A, 0x88);
            fill_rrect(&cr, nx, ny - 4, nw, 8, 3, col);
        }
    }
    fill_rect(&cr, head, roll.y, 2, roll.h, ARGB(200, 255, 255, 255));
    if (!st.loaded)
        draw_text_center(&cr, FONT_UI13, roll.x + roll.w / 2, roll.y + roll.h / 2 - 8, "Здесь побегут ноты", ARGB(160, 255, 255, 255));

    /* progress and controls */
    int by = roll.y + roll.h + 22;
    p->bar = (struct rect){ x0, by, rw, 6 };
    fill_rrect(c, x0, by, rw, 5, 2, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
    if (st.len_ms) {
        int fw = (int)((int64_t)rw * st.pos_ms / st.len_ms);
        if (fw > 0)
            fill_rrect(c, x0, by, MAX(fw, 5), 5, 2, OPAQUE(pal.accent));
        fill_circle(c, x0 + fw, by + 2.5, 6, OPAQUE(pal.accent));
    }
    char a[16], b[16];
    fmt(st.pos_ms, a, sizeof a);
    fmt(st.len_ms, b, sizeof b);
    draw_text(c, FONT_UI11, x0, by + 10, a, OPAQUE(pal.text2));
    draw_text(c, FONT_UI11, x0 + rw - text_width(FONT_UI11, b), by + 10, b, OPAQUE(pal.text2));
    int cx = x0 + rw / 2, cy = by + 48;
    p->b_prev = (struct rect){ cx - 80, cy - 18, 40, 36 };
    p->b_play = (struct rect){ cx - 24, cy - 24, 48, 48 };
    p->b_next = (struct rect){ cx + 40, cy - 18, 40, 36 };
    uint32_t tc = OPAQUE(pal.text);
    /* prev / next: two triangles and a bar */
    for (int side = -1; side <= 1; side += 2) {
        struct rect r = side < 0 ? p->b_prev : p->b_next;
        double gx = r.x + r.w / 2.0;
        for (int k = 0; k < 2; k++) {
            double ox = gx + side * (k * 8 - 5);
            for (double yy = -7; yy <= 7; yy += 0.5) {
                double ww = (1 - f_abs(yy) / 7.5) * 8;
                draw_line(c, ox - side * 4, cy + yy, ox - side * 4 + side * ww, cy + yy, 0.8, tc);
            }
        }
        fill_rect(c, (int)(gx + side * 11) - 1, cy - 7, 2, 14, tc);
    }
    fill_circle(c, cx, cy, 24, OPAQUE(pal.accent));
    if (st.playing) {
        fill_rrect(c, cx - 8, cy - 9, 5, 18, 1, 0xFFFFFFFF);
        fill_rrect(c, cx + 3, cy - 9, 5, 18, 1, 0xFFFFFFFF);
    } else {
        for (double yy = -10; yy <= 10; yy += 0.5) {
            double ww = (1 - f_abs(yy) / 10.5) * 17;
            draw_line(c, cx - 6, cy + yy, cx - 6 + ww, cy + yy, 0.8, 0xFFFFFFFF);
        }
    }
    draw_text_center(c, FONT_UI11, x0 + rw / 2, H - 22, "Звук идёт через PC-динамик (пищалку) компьютера, если он есть.",
                     OPAQUE(pal.text3));
}

static bool player_mouse(struct window *w, const struct mevent *e)
{
    struct pl *p = w->data;
    if (e->type == EV_MOVE) {
        int h = -1;
        for (int i = 0; i < nplay && i < 32; i++)
            if (rect_has(p->rows[i], e->x, e->y))
                h = i;
        if (h != p->hover) {
            p->hover = h;
            win_redraw(w);
        }
        return true;
    }
    if (e->type != EV_DOWN || e->button != 1)
        return false;
    for (int i = 0; i < nplay && i < 32; i++)
        if (rect_has(p->rows[i], e->x, e->y)) {
            if (i == st.track && st.loaded)
                player_toggle();
            else
                player_open(playlist[i]);
        }
    if (rect_has(p->b_play, e->x, e->y))
        player_toggle();
    else if (rect_has(p->b_prev, e->x, e->y))
        player_skip(-1);
    else if (rect_has(p->b_next, e->x, e->y))
        player_skip(1);
    win_redraw(w);
    return true;
}

static void player_key(struct window *w, const struct key *k)
{
    if (k->code == KEY_CHAR && k->cp == ' ')
        player_toggle();
    else if (k->code == KEY_LEFT)
        player_skip(-1);
    else if (k->code == KEY_RIGHT)
        player_skip(1);
    win_redraw(w);
}

static void player_wtick(struct window *w, uint64_t now)
{
    struct pl *p = w->data;
    if ((st.playing || p->last == 0) && now - p->last >= 40) {
        p->last = now;
        win_redraw(w);
    }
}

static struct window *player_win;

static void player_closed(struct window *w)
{
    kfree(w->data);
    player_win = NULL;
}

static void player_launch(const char *arg)
{
    if (arg && *arg)
        player_open(arg);
    if (player_win) {
        win_focus(player_win);
        return;
    }
    struct pl *p = kzalloc(sizeof *p);
    if (!p)
        return;
    p->hover = -1;
    struct window *w = win_open(&app_player, "Плеер", 720, 470, WF_RESIZABLE);
    if (!w) {
        kfree(p);
        return;
    }
    w->min_w = 560;
    w->min_h = 400;
    w->data = p;
    w->draw = player_draw;
    w->mouse = player_mouse;
    w->key = player_key;
    w->tick = player_wtick;
    w->closed = player_closed;
    player_win = w;
}

enum { ACT_PLAY = 1, ACT_PREV, ACT_NEXT };

static void player_action(struct window *w, int a)
{
    if (a == ACT_PLAY)
        player_toggle();
    else if (a == ACT_PREV)
        player_skip(-1);
    else if (a == ACT_NEXT)
        player_skip(1);
    if (w)
        win_redraw(w);
}

static const struct menu_item ctl_items[] = {
    { "Играть / пауза", "Пробел", ACT_PLAY },
    { "Предыдущая", "←", ACT_PREV },
    { "Следующая", "→", ACT_NEXT },
    { NULL, NULL, 0 },
};
static const struct menu player_menus[] = {
    { "Управление", ctl_items },
    { NULL, NULL },
};

const struct app_def app_player = {
    "player", "Плеер", "Играет мелодии .mel из папки «Музыка» на PC-динамике. Ноты можно писать в «Тексте».",
    ICON_PLAYER, player_launch, player_menus, player_action,
};
