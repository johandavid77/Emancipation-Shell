#include "bar/bar.h"
#include "bar/sysinfo.h"
#include "bar/backends.h"
#include "bar/workspaces.h"
#include "launcher/launcher.h"
#include "launcher/desktop.h"
#include "launcher/icons.h"
#include "compositors/niri_ipc.h"
#include "systray/systray.h"
#include "config/config.h"
#include "util/weather.h"
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Workspace pill proportions follow Noctalia's "regular" style
 * (workspaces_widget.h/.cpp): active pill 2.2x the base, inactive 1.0x,
 * 4px gap, label text ~0.8 of body text. */
#define WS_GAP 4.0
#define WS_ACTIVE_MUL 2.2
#define WS_LABEL_PAD 6.0
#define WS_EMPTY_ALPHA 0.55

struct draw_env {
    cairo_t *cr;
    PangoLayout *text;  /* body font */
    PangoLayout *small; /* workspace labels */
    const struct bar_ctx *ctx;
    const struct theme_config *th;
    int h;
    struct bar_hits *hits; /* NULL during the measure pass */
};

static void set_rgba(cairo_t *cr, struct color_rgba c, double alpha_mul)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_mul);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static double text_width(PangoLayout *l, const char *txt)
{
    int tw, th;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &tw, &th);
    return tw;
}

static void text_draw(struct draw_env *e, PangoLayout *l, const char *txt, double x)
{
    int tw, th;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &tw, &th);
    cairo_move_to(e->cr, x, (e->h - th) / 2.0);
    pango_cairo_show_layout(e->cr, l);
}

static void add_hit(struct draw_env *e, double x0, double x1, enum bar_hit_kind k, void *ref)
{
    if (!e->hits || e->hits->n >= BAR_MAX_HITS) return;
    e->hits->h[e->hits->n++] = (struct bar_hit){ x0, x1, k, ref };
}

static bool hovered(const struct draw_env *e, double x0, double x1)
{
    return e->ctx->hover_x >= x0 && e->ctx->hover_x < x1;
}

/* ---- widgets: each returns its width; draws only when draw != 0 -------- */

static double w_workspaces(struct draw_env *e, double x, int draw)
{
    struct workspace_info ws[32];
    size_t n = workspaces_snapshot(e->ctx->wm, e->ctx->output, ws, 32);
    double pill_h = e->h * 0.56;
    double y = (e->h - pill_h) / 2.0;
    double cx = x;
    for (size_t i = 0; i < n; i++) {
        const char *label = ws[i].label[0] ? ws[i].label : "?";
        double tw = text_width(e->small, label);
        double base = pill_h > tw + 2 * WS_LABEL_PAD ? pill_h : tw + 2 * WS_LABEL_PAD;
        double pw = ws[i].active ? pill_h * WS_ACTIVE_MUL : base;
        if (pw < tw + 2 * WS_LABEL_PAD) pw = tw + 2 * WS_LABEL_PAD;
        if (draw) {
            struct color_rgba fill, ink;
            double alpha = 1.0;
            if (ws[i].active) {
                fill = e->th->primary;
                ink = e->th->on_primary;
            } else if (ws[i].urgent) {
                fill = e->th->error;
                ink = e->th->on_error;
            } else {
                fill = e->th->secondary;
                ink = e->th->on_secondary;
                alpha = ws[i].hidden ? WS_EMPTY_ALPHA : 0.8;
            }
            if (hovered(e, cx, cx + pw) && !ws[i].active) alpha = 1.0;
            set_rgba(e->cr, fill, alpha);
            rounded_rect(e->cr, cx, y, pw, pill_h, pill_h / 2);
            cairo_fill(e->cr);
            set_rgba(e->cr, ink, 1.0);
            text_draw(e, e->small, label, cx + (pw - tw) / 2.0);
            add_hit(e, cx, cx + pw, BAR_HIT_WORKSPACE, ws[i].ref);
        }
        cx += pw + (i + 1 < n ? WS_GAP : 0);
    }
    return cx - x;
}

static double w_time(struct draw_env *e, double x, int draw, const char *fmt, double alpha, enum bar_hit_kind hit)
{
    char buf[128];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (strftime(buf, sizeof(buf), fmt, &tmv) == 0) buf[0] = '\0';
    double tw = text_width(e->text, buf);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, alpha);
        text_draw(e, e->text, buf, x);
        add_hit(e, x - 8, x + tw + 8, hit, NULL);
    }
    return tw;
}

/* Taskbar: show window name (icon + title) as requested */
static double w_taskbar(struct draw_env *e, double x, int draw)
{
    struct niri_ipc *n = e->ctx->niri;
    if (!n) return 0.0;
    int count = niri_ipc_window_count(n);
    if (count <= 0) return 0.0;
    double icon_px = 18.0;
    double gap = 6.0;
    double pad = 6.0;
    double cur = x;

    if (!draw) {
        double total = 0.0;
        for (int i = 0; i < count; i++) {
            const struct niri_window *w = niri_ipc_window(n, i);
            if (!w) continue;
            char *ip = icon_resolve_path(w->app_id);
            cairo_surface_t *icon = ip ? icon_load_surface(ip, (int)icon_px) : NULL;
            free(ip);
            const char *title = w->title[0] ? w->title : (w->app_id[0] ? w->app_id : "win");
            char buf[48];
            snprintf(buf, sizeof(buf), "%s", title);
            double tw = text_width(e->text, buf);
            double iw = icon ? icon_px : 0.0;
            total += iw + (iw ? 4.0 : 0.0) + tw + pad * 2 + gap;
        }
        if (total > 0) total -= gap;
        return total;
    }

    double total_draw = 0.0;
    for (int i = 0; i < count; i++) {
        const struct niri_window *w = niri_ipc_window(n, i);
        if (!w) continue;
        char *ip = icon_resolve_path(w->app_id);
        cairo_surface_t *icon = ip ? icon_load_surface(ip, (int)icon_px) : NULL;
        free(ip);
        const char *title = w->title[0] ? w->title : (w->app_id[0] ? w->app_id : "win");
        char buf[48];
        snprintf(buf, sizeof(buf), "%s", title);
        double tw = text_width(e->text, buf);
        double iw = icon ? icon_px : 0.0;
        double bx = cur;
        double bw = iw + (iw ? 4.0 : 0.0) + tw + pad * 2;
        if (e->hits) {
            struct bar_hit h = {bx, bx + bw, BAR_HIT_TASKBAR, (void *)(intptr_t)i};
            if (e->hits->n < BAR_MAX_HITS) e->hits->h[e->hits->n++] = h;
        }
        if (w->focused) {
            set_rgba(e->cr, e->th->primary, 0.9);
            cairo_rectangle(e->cr, bx, e->h - 3, bw, 2);
            cairo_fill(e->cr);
            set_rgba(e->cr, e->th->foreground, 1.0);
        } else {
            set_rgba(e->cr, e->th->foreground, 0.75);
        }
        if (icon) {
            cairo_set_source_surface(e->cr, icon, bx + pad, (e->h - icon_px) / 2.0);
            cairo_paint(e->cr);
            cairo_surface_destroy(icon);
            text_draw(e, e->text, buf, bx + pad + iw + 4.0);
        } else {
            text_draw(e, e->text, buf, bx + pad);
        }
        cur += bw + gap;
        total_draw += bw + gap;
    }
    if (total_draw > 0) total_draw -= gap;
    return total_draw;
}
static double w_launcher(struct draw_env *e, double x, int draw)
{
    bool vis = e->ctx->launcher && launcher_is_visible(e->ctx->launcher);
    const char *txt = vis ? "v" : ">";
    double tw = text_width(e->text, txt);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, 0.95);
        text_draw(e, e->text, txt, x);
        add_hit(e, x - 8, x + tw + 8, BAR_HIT_LAUNCHER, NULL);
    }
    return tw;
}

static double w_volume(struct draw_env *e, double x, int draw)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b || !b->vol_available || b->vol_pct < 0) return 0;
    const char *ic;
    if (b->vol_muted || b->vol_pct == 0)
        ic = "\xF0\x9F\x94\x87";            /* 🔇 */
    else if (b->vol_pct < 33)
        ic = "\xF0\x9F\x94\x88";            /* 🔈 */
    else if (b->vol_pct < 66)
        ic = "\xF0\x9F\x94\x89";            /* 🔉 */
    else
        ic = "\xF0\x9F\x94\x8A";            /* 🔊 */
    char val[16];
    snprintf(val, sizeof(val), "%d%%", b->vol_pct);
    double iw = text_width(e->text, ic);
    double gap = 5.0;
    double vw = text_width(e->text, val);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->text, ic, x);
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, val, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + vw + 4, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return iw + gap + vw;
}

static double w_brightness(struct draw_env *e, double x, int draw)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b || !b->br_available || b->br_pct < 0) return 0;
    const char *ic = "\xF0\x9F\x94\x86"; /* 🔆 */
    char val[16];
    snprintf(val, sizeof(val), "%d%%", b->br_pct);
    double iw = text_width(e->text, ic);
    double gap = 5.0;
    double vw = text_width(e->text, val);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->text, ic, x);
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, val, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + vw + 4, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return iw + gap + vw;
}

static double w_network(struct draw_env *e, double x, int draw)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b || !b->net_connected) return 0;
    const char *ic;
    char txt[48];
    if (b->net_kind == NET_WIFI) {
        ic = "\xF0\x9F\x93\xB6"; /* 📶 */
        if (b->net_ssid[0]) snprintf(txt, sizeof(txt), "%s", b->net_ssid);
        else if (b->net_strength >= 0) snprintf(txt, sizeof(txt), "%d%%", b->net_strength);
        else snprintf(txt, sizeof(txt), "wifi");
    } else if (b->net_kind == NET_ETHERNET) {
        ic = "\xF0\x9F\x94\x8C"; /* 🔌 */
        snprintf(txt, sizeof(txt), "eth");
    } else {
        ic = "\xF0\x9F\x8C\x90"; /* 🌐 */
        snprintf(txt, sizeof(txt), "%s", b->net_if);
    }
    double iw = text_width(e->text, ic);
    double gap = 5.0;
    double tw = text_width(e->small, txt);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->text, ic, x);
        set_rgba(e->cr, e->th->foreground, 0.9);
        text_draw(e, e->small, txt, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + tw + 4, BAR_HIT_NETWORK, NULL);
    }
    return iw + gap + tw;
}

static double w_media(struct draw_env *e, double x, int draw)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b || !b->media_present || !b->media_title[0]) return 0;
    const char *ic = b->media_playing ? "\xE2\x96\xB6" /* ▶ */ : "\xE2\x8F\xB8"; /* ⏸ */
    char buf[64];
    if (b->media_artist[0])
        snprintf(buf, sizeof(buf), "%s - %s", b->media_artist, b->media_title);
    else
        snprintf(buf, sizeof(buf), "%s", b->media_title);
    double iw = text_width(e->text, ic);
    double gap = 5.0;
    double tw = text_width(e->text, buf);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->text, ic, x);
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, buf, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + tw + 4, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return iw + gap + tw;
}

static double w_clipboard(struct draw_env *e, double x, int draw)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b || b->clipboard_count <= 0) return 0;
    const char *ic = "\xF0\x9F\x93\x8B"; /* 📋 */
    char val[16];
    snprintf(val, sizeof(val), "%d", b->clipboard_count);
    double iw = text_width(e->text, ic);
    double gap = 4.0;
    double vw = text_width(e->small, val);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->text, ic, x);
        set_rgba(e->cr, e->th->foreground, 0.9);
        text_draw(e, e->small, val, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + vw + 4, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return iw + gap + vw;
}

/* User-defined widget: the module name is a shell command; its stdout is
 * rendered in the bar. */
static double w_script(struct draw_env *e, double x, int draw, const char *cmd)
{
    const struct backends_state *b = e->ctx->bk;
    if (!b) return 0;
    const char *out = backends_script_output(b, cmd);
    if (!out || !out[0]) return 0;
    double tw = text_width(e->text, out);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, 0.95);
        text_draw(e, e->text, out, x);
    }
    return tw;
}

static double w_tray(struct draw_env *e, double x, int draw)
{
    struct systray *st = e->ctx->systray;
    if (!st) return 0;
    int n = systray_item_count(st);
    double w = n > 0 ? systray_width(st) : 18.0;
    if (!draw) return w;
    if (n > 0) {
        systray_draw(st, e->cr, x, e->h);
    } else {
        /* idle tray: three dots so the region is always visible */
        set_rgba(e->cr, e->th->on_surface_variant, 0.9);
        for (int i = 0; i < 3; i++) {
            cairo_arc(e->cr, x + 3 + i * 5.5, e->h / 2.0, 1.6, 0, 2 * G_PI);
            cairo_fill(e->cr);
        }
    }
    return w;
}

/* Three slider tracks with knobs: the control center affordance. */
static double w_control_center(struct draw_env *e, double x, int draw)
{
    double w = 18.0;
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        cairo_set_line_width(e->cr, 1.5);
        for (int i = 0; i < 3; i++) {
            double ly = e->h / 2.0 - 5 + i * 5;
            cairo_move_to(e->cr, x, ly);
            cairo_line_to(e->cr, x + w, ly);
            cairo_stroke(e->cr);
            double kx = x + (i == 0 ? 5.0 : i == 1 ? 13.0 : 8.0);
            cairo_arc(e->cr, kx, ly, 2.3, 0, 2 * G_PI);
            cairo_fill(e->cr);
        }
        add_hit(e, x - 6, x + w + 6, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return w;
}

static double w_kbd(struct draw_env *e, double x, int draw)
{
    const char *txt = "US";
    double tw = text_width(e->small, txt);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, txt, x);
        add_hit(e, x - 2, x + tw + 2, BAR_HIT_KBD, NULL);
    }
    return tw;
}
/* "LABEL value": label dimmed like Noctalia's on_surface_variant icons */
static double w_metric(struct draw_env *e, double x, int draw, const char *label, int pct, const char *suffix)
{
    if (pct < 0) return 0;
    char val[32];
    snprintf(val, sizeof(val), "%d%%%s", pct, suffix ? suffix : "");
    double lw = text_width(e->small, label);
    double gap = 5;
    double vw = text_width(e->text, val);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, label, x);
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, val, x + lw + gap);
    }
    return lw + gap + vw;
}

#define WEATHER_CACHE "/tmp/emancipation-weather.txt"

/* Reads the shared weather cache (city|temp|code|hum|wind|is_day|moon_phase). */
static double w_weather(struct draw_env *e, double x, int draw)
{
    if (!e->ctx->cfg || !e->ctx->cfg->bar.show_weather) return 0;
    FILE *f = fopen(WEATHER_CACHE, "r");
    if (!f) return 0;
    char buf[256];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    if (!buf[0]) return 0;

    char *parts[7] = {0};
    int np = 0;
    char *save = buf;
    while (np < 7) {
        parts[np++] = save;
        char *sep = strchr(save, '|');
        if (!sep) break;
        *sep = '\0';
        save = sep + 1;
    }
    if (np < 3) return 0;

    int is_day = np >= 6 ? atoi(parts[5]) : 1;
    const char *ic = weather_glyph(atoi(parts[2]), is_day, np >= 7 ? atof(parts[6]) : 0.5);
    char temp[32];
    snprintf(temp, sizeof(temp), "%.0f°", atof(parts[1]));
    double iw = text_width(e->text, ic);
    double gap = 5.0;
    double tw = text_width(e->text, temp);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, ic, x);
        text_draw(e, e->text, temp, x + iw + gap);
        add_hit(e, x - 4, x + iw + gap + tw + 4, BAR_HIT_CONTROL_CENTER, NULL);
    }
    return iw + gap + tw;
}

static double widget(struct draw_env *e, const char *name, double x, int draw)
{
    const struct config *c = e->ctx->cfg;
    const struct sysinfo_state *s = e->ctx->sys;
    if (strcmp(name, "launcher") == 0) return w_launcher(e, x, draw);
    if (strcmp(name, "workspaces") == 0) return w_workspaces(e, x, draw);
    if (strcmp(name, "clock") == 0) return w_time(e, x, draw, c->bar.clock_format, 1.0, BAR_HIT_CLOCK);
    if (strcmp(name, "date") == 0) return w_time(e, x, draw, c->bar.date_format, 0.8, BAR_HIT_DATE);
    if (strcmp(name, "tray") == 0) return w_tray(e, x, draw);
    if (strcmp(name, "kbd") == 0 || strcmp(name, "keyboard") == 0 || strcmp(name, "keyboard_layout") == 0) return w_kbd(e, x, draw);
    if (strcmp(name, "taskbar") == 0) return w_taskbar(e, x, draw);
    if (strcmp(name, "control_center") == 0) return w_control_center(e, x, draw);
    if (strcmp(name, "weather") == 0) return w_weather(e, x, draw);
    if (strcmp(name, "volume") == 0) return w_volume(e, x, draw);
    if (strcmp(name, "network") == 0) return w_network(e, x, draw);
    if (strcmp(name, "brightness") == 0) return w_brightness(e, x, draw);
    if (strcmp(name, "media") == 0) return w_media(e, x, draw);
    if (strcmp(name, "clipboard") == 0) return w_clipboard(e, x, draw);
    if (!backends_is_builtin_module(name)) return w_script(e, x, draw, name);
    if (!s) return 0;
    if (strcmp(name, "cpu") == 0) return w_metric(e, x, draw, "CPU", s->cpu_pct, NULL);
    if (strcmp(name, "ram") == 0) return w_metric(e, x, draw, "RAM", s->ram_pct, NULL);
    if (strcmp(name, "battery") == 0)
        return w_metric(e, x, draw, "BAT", s->bat_pct, s->bat_charging ? "+" : NULL);
    return 0; /* unknown module: ignored */
}

/* Lay out a section starting at x; returns total width. */
static double section(struct draw_env *e, enum bar_section sec, double x, int draw)
{
    const struct bar_config *b = &e->ctx->cfg->bar;
    double cx = x;
    bool first = true;
    for (int i = 0; i < b->n_modules[sec]; i++) {
        double probe = widget(e, b->modules[sec][i], 0, 0);
        if (probe <= 0) continue; /* hidden widgets take no spacing */
        if (!first) cx += b->spacing;
        widget(e, b->modules[sec][i], cx, draw);
        cx += probe;
        first = false;
    }
    return cx - x;
}

static PangoLayout *make_layout(cairo_t *cr, const char *family, double size, PangoWeight weight)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(fd, weight);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    return l;
}

/* Lay out BAR_CENTER keeping the "clock" pinned to the screen centre: other
 * centre widgets (e.g. media) hang to its left/right without moving the clock.
 * Falls back to centring the whole group when there is no clock. */
static void center_layout(struct draw_env *e, double w, int draw, double shift,
                          double *out_x, double *out_w)
{
    const struct bar_config *b = &e->ctx->cfg->bar;
    int n = b->n_modules[BAR_CENTER];
    double widths[BAR_MAX_MODULES];
    for (int i = 0; i < n; i++) widths[i] = widget(e, b->modules[BAR_CENTER][i], 0, 0);

    int ci = -1;
    for (int i = 0; i < n; i++)
        if (strcmp(b->modules[BAR_CENTER][i], "clock") == 0) { ci = i; break; }

    if (ci < 0) {
        double group = 0; bool first = true;
        for (int i = 0; i < n; i++) {
            if (widths[i] <= 0) continue;
            if (!first) group += b->spacing;
            group += widths[i];
            first = false;
        }
        double gx = (w - group) / 2.0 + shift;
        if (draw) section(e, BAR_CENTER, gx, 1);
        if (out_x) *out_x = gx;
        if (out_w) *out_w = group;
        return;
    }

    double clock_w = widths[ci];
    double clock_x = (w - clock_w) / 2.0 + shift;
    double left = 0, right = 0;
    for (int i = ci - 1; i >= 0; i--) if (widths[i] > 0) left += widths[i] + b->spacing;
    for (int i = ci + 1; i < n; i++) if (widths[i] > 0) right += widths[i] + b->spacing;

    if (draw) {
        double x = clock_x + clock_w + b->spacing;
        for (int i = ci + 1; i < n; i++) {
            if (widths[i] <= 0) continue;
            widget(e, b->modules[BAR_CENTER][i], x, draw);
            x += widths[i] + b->spacing;
        }
        widget(e, b->modules[BAR_CENTER][ci], clock_x, draw);
        x = clock_x - b->spacing;
        for (int i = ci - 1; i >= 0; i--) {
            if (widths[i] <= 0) continue;
            x -= widths[i];
            widget(e, b->modules[BAR_CENTER][i], x, draw);
            x -= b->spacing;
        }
    }
    if (out_x) *out_x = clock_x - left;
    if (out_w) *out_w = left + clock_w + right;
}

void bar_draw(cairo_t *cr, int w, int h, const struct bar_ctx *ctx, struct bar_hits *hits)
{
    struct config def;
    struct bar_ctx local = *ctx;
    if (!local.cfg) {
        config_defaults(&def);
        local.cfg = &def;
    }
    const struct config *cfg = local.cfg;
    if (hits) hits->n = 0;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_rgba(cr, cfg->theme.background, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    const char *family = cfg->font.family[0] ? cfg->font.family : "sans-serif";
    double size = cfg->font.size > 0 ? cfg->font.size : 11;
    struct draw_env e = {
        .cr = cr,
        .text = make_layout(cr, family, size, PANGO_WEIGHT_MEDIUM), /* Noctalia: weight 500 */
        .small = make_layout(cr, family, size * 0.8, PANGO_WEIGHT_BOLD),
        .ctx = &local,
        .th = &cfg->theme,
        .h = h,
        .hits = NULL,
    };

    double pad = cfg->bar.padding;
    double sw = section(&e, BAR_START, 0, 0);
    double ew = section(&e, BAR_END, 0, 0);
    double ccx = 0, ccw = 0;
    center_layout(&e, w, 0, 0, &ccx, &ccw);

    double sx = pad;
    double ex = w - pad - ew;
    double shift = 0;
    if (ccx < sx + sw + cfg->bar.spacing) shift = (sx + sw + cfg->bar.spacing) - ccx;

    e.hits = hits;
    section(&e, BAR_START, sx, 1);
    center_layout(&e, w, 1, shift, NULL, NULL);
    if (ex >= ccx + shift + ccw + cfg->bar.spacing) section(&e, BAR_END, ex, 1); /* drop when it would overlap */

    g_object_unref(e.text);
    g_object_unref(e.small);
}

const struct bar_hit *bar_hit_at(const struct bar_hits *hits, double x)
{
    if (!hits) return NULL;
    for (int i = 0; i < hits->n; i++) {
        if (x >= hits->h[i].x0 && x < hits->h[i].x1) return &hits->h[i];
    }
    return NULL;
}
