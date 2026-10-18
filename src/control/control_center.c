#define _GNU_SOURCE
#include "control/control_center.h"
#include "core/output.h"
#include "core/panel.h"
#include "config/config.h"
#include "wallpaper/wallpaper.h"
#include "session/session_actions.h"
#include "session/lock_screen.h"
#include "notify/notification.h"
#include "compositors/niri_ipc.h"
#include "util/weather.h"
#include "util/log.h"

#include <cairo.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

/* Quick-settings overlay: the Noctalia control center (specs/features/005).
 * A side panel with four tabs (home / notifications / shortcuts / screen) and,
 * on the home tab, Wi-Fi, Bluetooth, dark mode, mute, night light and power
 * profile toggles, volume/brightness sliders, wallpaper controls, session
 * buttons and a weather card. */

#define CC_W 380
#define CC_H 700
#define CC_PAD 16.0
#define CC_TITLE_H 26.0
#define CC_TAB_H 34.0
#define CC_TAB_GAP 6.0
#define CC_GAP 14.0
#define CC_TILE_GAP 10.0
#define CC_TILE_H 56.0
#define CC_SLIDER_H 46.0
#define CC_SLIDER_GAP 10.0
#define CC_POWER_H 40.0
#define CC_POWER_GAP 8.0
#define CC_PWR_N 5
#define CC_LOCK_H 52.0

enum cc_tab { TAB_HOME = 0, TAB_NOTIF, TAB_SHORTCUTS, TAB_SCREEN, TAB_MAX };

static const char *cc_tab_name(int t)
{
    switch (t) {
        case TAB_HOME: return "Inicio";
        case TAB_NOTIF: return "Notif.";
        case TAB_SHORTCUTS: return "Atajos";
        case TAB_SCREEN: return "Pantalla";
        default: return "";
    }
}

enum cc_toggle {
    CC_WIFI = 0,
    CC_BT,
    CC_DARK,
    CC_MUTE,
    CC_NIGHT,
    CC_POWER,
    CC_BAR_WEA,
    CC_WP_DIR,
    CC_TOGGLE_MAX
};

enum cc_power_btn { PWR_LOCK = 0, PWR_SUSPEND, PWR_LOGOUT, PWR_REBOOT, PWR_OFF };

enum cc_hover {
    CC_HOVER_NONE = -1,
    CC_HOVER_VOLUME = 100,
    CC_HOVER_BRIGHTNESS = 101,
    CC_HOVER_POWER_BASE = 200,
    CC_HOVER_TAB_BASE = 300,
    CC_HOVER_SC_BASE = 400,
    CC_HOVER_NF_BASE = 500,
    CC_HOVER_WIN_BASE = 600,
    CC_HOVER_NF_CLEAR = 700,
    CC_HOVER_LOCK = 800,
};

struct cc_geom {
    double tile_x[CC_TOGGLE_MAX], tile_y[CC_TOGGLE_MAX], tile_w, tile_h;
    double vol_x, vol_y, vol_w, vol_h;
    double bri_x, bri_y, bri_w, bri_h;
    double pwr_x[CC_PWR_N], pwr_y, pwr_w, pwr_h;
    double wp_lbl_y, wp_btn_y, wp_btn_h;
    double wth_x, wth_y, wth_w, wth_h;
};

struct cc_shortcut {
    const char *label;
    const char *glyph;
    const char *cmd;
};

#define CC_SC_N 10
static const struct cc_shortcut cc_shortcuts[CC_SC_N] = {
    { "Terminal",  "T", "foot || alacritty || kitty || xterm || st" },
    { "Archivos",  "F", "thunar || pcmanfm || nautilus || xdg-open \"$HOME\"" },
    { "Navegador", "W", "firefox || chromium || xdg-open https://duckduckgo.com" },
    { "Editor",    "E", "gnome-text-editor || gedit || kate || xed || mousepad || featherpad" },
    { "Captura",   "C", "mkdir -p \"$HOME/Pictures\"; grim \"$HOME/Pictures/screenshot-$(date +%Y%m%d-%H%M%S).png\"" },
    { "Bloquear",  "L", "@lock" },
    { "Lanzador",  "K", "@launcher" },
    { "Pantalla",  "S", "@screen_off" },
    { "Red",       "N", "@network" },
    { "Bluetooth", "B", "@bluetooth" },
};

struct control_center {
    struct panel panel;
    struct output *fallback_out;
    const struct config *cfg;
    struct session_actions *sa;
    struct lock_screen *lock;
    const struct notification_store *nstore;
    struct niri_ipc *niri;
    void (*shortcut_fn)(void *userdata, const char *id);
    void *shortcut_ud;

    PangoLayout *l_title, *l_body, *l_small;

    int tab;
    int hover_tab;

    bool wifi, bt, dark, mute, night;
    bool lock_enabled; /* mirror of config lock.enabled */
    bool bar_weather; /* mirror of config bar.show_weather */
    char wp_dir[512]; /* mirror of config wallpaper.dir */
    int power;      /* 0 balanced, 1 performance, 2 power-saver */
    int volume;     /* 0..100 */
    int brightness; /* 0..100 */
    int hover;
    int last_ncount;
    time_t start_time;
    char w_loc[64];
    char w_temp[24];
    char w_cond[48];
    char w_extra[64];
    char w_icon[16];
    char w_moon[32];
    double w_moon_phase;
    bool w_ok;
    time_t w_mtime;
    time_t w_fetched;
};

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

#define CC_WEATHER_CACHE "/tmp/emancipation-weather.txt"
#define CC_WEATHER_TTL   1800

static void cc_weather_refresh(void)
{
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int dn = open("/dev/null", O_RDWR);
    if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
    execl("/bin/sh", "sh", "-c",
          "python3 -c \""
          "import urllib.request,json;"
          "j=lambda u:json.loads(urllib.request.urlopen(u,timeout=12).read().decode());"
          "g=j('https://ipinfo.io/json');"
          "lat,lon=g.get('loc','0,0').split(',');"
           "w=j('https://api.open-meteo.com/v1/forecast?latitude='+lat+'&longitude='+lon"
           "+'&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code,is_day&daily=moon_phase&timezone=auto');"
           "c=w['current'];"
           "m=w.get('daily',{}).get('moon_phase',[0.5])[0] if 'daily' in w else 0.5;"
           "print('%s|%.1f|%d|%d|%.0f|%d|%.3f' % (g.get('city',''),c['temperature_2m'],"
           "int(c['weather_code']),int(c['relative_humidity_2m']),c['wind_speed_10m'],"
           "int(c.get('is_day',1)),m),end='')\""
          " > " CC_WEATHER_CACHE ".tmp"
          " && mv " CC_WEATHER_CACHE ".tmp " CC_WEATHER_CACHE,
          (char *)NULL);
    _exit(127);
}

/* Map WMO weather code to an emoji glyph and a Spanish label. */
static const char *cc_weather_icon(int code, int is_day, double moon_phase)
{
    return weather_glyph(code, is_day, moon_phase);
}

static const char *cc_moon_icon(double phase)
{
    return weather_moon_glyph(phase);
}

static void cc_moon_name(double phase, char *out, size_t n)
{
    if (phase < 0.02 || phase >= 0.98) snprintf(out, n, "Luna nueva");
    else if (phase < 0.23) snprintf(out, n, "Creciente");
    else if (phase < 0.27) snprintf(out, n, "Cuarto creciente");
    else if (phase < 0.48) snprintf(out, n, "Gibosa creciente");
    else if (phase < 0.52) snprintf(out, n, "Llena");
    else if (phase < 0.73) snprintf(out, n, "Gibosa menguante");
    else if (phase < 0.77) snprintf(out, n, "Cuarto menguante");
    else snprintf(out, n, "Menguante");
}

static void cc_weather_load(struct control_center *cc)
{
    struct stat st;
    if (stat(CC_WEATHER_CACHE, &st) != 0) return;
    if (st.st_mtime == cc->w_mtime) return;
    FILE *f = fopen(CC_WEATHER_CACHE, "r");
    if (!f) return;
    char buf[256];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    cc->w_mtime = st.st_mtime;
    if (!buf[0]) return;

    char *parts[7] = {0};
    int np = 0;
    char *save = buf;
    while (np < 7) {
        parts[np++] = save;
        char *bar = strchr(save, '|');
        if (!bar) break;
        *bar = '\0';
        save = bar + 1;
    }
    if (np < 3) return;
    char *loc = parts[0];
    char *comma = strchr(loc, ',');
    if (comma) *comma = '\0';
    while (*loc == ' ') loc++;
    snprintf(cc->w_loc, sizeof(cc->w_loc), "%.*s", (int)sizeof(cc->w_loc) - 1, loc);
    int code = atoi(parts[2]);
    int is_day = (np >= 6) ? atoi(parts[5]) : 1;
    cc->w_moon_phase = (np >= 7) ? atof(parts[6]) : 0.5;
    cc_moon_name(cc->w_moon_phase, cc->w_moon, sizeof(cc->w_moon));
    double tp = atof(parts[1]);
    snprintf(cc->w_temp, sizeof(cc->w_temp), "%d\xC2\xB0""C", (int)(tp + (tp >= 0 ? 0.5 : -0.5)));
    weather_label_es(code, cc->w_cond, sizeof(cc->w_cond));
    const char *icon = cc_weather_icon(code, is_day, cc->w_moon_phase);
    snprintf(cc->w_icon, sizeof(cc->w_icon), "%s", icon);
    char extra[64] = "";
    if (np >= 5) snprintf(extra, sizeof(extra), "H %s%%   V %s km/h", parts[3], parts[4]);
    else if (np >= 4) snprintf(extra, sizeof(extra), "Humedad %s%%", parts[3]);
    snprintf(cc->w_extra, sizeof(cc->w_extra), "%s", extra);
    cc->w_ok = true;
}

static void cc_exec(const char *cmd)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
}

static bool cc_run(const char *cmd, char *out, size_t n)
{
    if (out && n) out[0] = '\0';
    FILE *f = popen(cmd, "r");
    if (!f) return false;
    size_t r = 0;
    if (out && n > 1) r = fread(out, 1, n - 1, f);
    if (out) out[r] = '\0';
    int rc = pclose(f);
    return rc == 0;
}

static void cc_query_state(struct control_center *cc)
{
    char buf[256];
    if (cc_run("nmcli -t radio wifi 2>/dev/null", buf, sizeof(buf)))
        cc->wifi = strstr(buf, "enabled") != NULL;
    if (cc_run("bluetoothctl show 2>/dev/null", buf, sizeof(buf)))
        cc->bt = strstr(buf, "Powered: yes") != NULL;
    if (cc_run("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf))) {
        cc->mute = strstr(buf, "MUTED") != NULL;
        double v = 0;
        if (sscanf(buf, "Volume: %lf", &v) == 1) cc->volume = (int)(v * 100 + 0.5);
    }
    if (cc_run("powerprofilesctl get 2>/dev/null", buf, sizeof(buf))) {
        if (strstr(buf, "performance")) cc->power = 1;
        else if (strstr(buf, "power-saver")) cc->power = 2;
        else cc->power = 0;
    }
    char cur[32] = "";
    char maxs[32] = "";
    if (cc_run("brightnessctl -c backlight get 2>/dev/null", cur, sizeof(cur)) &&
        cc_run("brightnessctl -c backlight max 2>/dev/null", maxs, sizeof(maxs))) {
        int c = atoi(cur), m = atoi(maxs);
        if (m > 0) cc->brightness = (int)(100.0 * c / m + 0.5);
    }
}

#define CC_WP_DIRS_MAX 8

static int cc_wallpaper_dirs(char dirs[][512], int max)
{
    const char *home = getenv("HOME");
    if (!home) home = "";
    const char *rel[] = {
        "Pictures/Wallpapers", "Pictures/wallpapers", "Pictures", "Wallpapers",
        ".config/emancipation/wallpapers", ".local/share/wallpapers",
    };
    int n = 0;
    for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]) && n < max; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", home, rel[i]);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(dirs[n++], 512, "%s", p);
    }
    const char *sys[] = { "/usr/share/backgrounds", "/usr/share/wallpapers" };
    for (size_t i = 0; i < sizeof(sys) / sizeof(sys[0]) && n < max; i++) {
        struct stat st;
        if (stat(sys[i], &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(dirs[n++], 512, "%s", sys[i]);
    }
    return n;
}

static const char *cc_dir_label(const struct control_center *cc, char *buf, size_t n)
{
    const char *d = cc->wp_dir;
    struct wallpaper *wp = wallpaper_instance_get();
    if (!d[0] && wp) d = wallpaper_get_dir(wp);
    if (!d[0]) d = "Auto";
    const char *b = strrchr(d, '/');
    if (b && b[1]) d = b + 1;
    size_t len = strlen(d);
    if (len >= n) len = n - 1;
    memcpy(buf, d, len);
    buf[len] = '\0';
    return buf;
}

static void cc_cycle_wallpaper_dir(struct control_center *cc)
{
    char dirs[CC_WP_DIRS_MAX][512];
    int n = cc_wallpaper_dirs(dirs, CC_WP_DIRS_MAX);
    if (n == 0) return;
    struct wallpaper *wp = wallpaper_instance_get();
    const char *cur = wp ? wallpaper_get_dir(wp) : "";
    int idx = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(dirs[i], cur) == 0) { idx = i; break; }
    }
    int next = (idx + 1) % n;
    snprintf(cc->wp_dir, sizeof(cc->wp_dir), "%.511s", dirs[next]);
    if (wp) {
        wallpaper_set_dir(wp, dirs[next]);
        wallpaper_next(wp);
    }
    config_write_wallpaper_dir(dirs[next]);
}

static void cc_apply_toggle(struct control_center *cc, int idx)
{
    switch (idx) {
        case CC_WIFI:
            cc->wifi = !cc->wifi;
            cc_exec(cc->wifi ? "nmcli radio wifi on" : "nmcli radio wifi off");
            break;
        case CC_BT:
            cc->bt = !cc->bt;
            cc_exec(cc->bt ? "bluetoothctl power on" : "bluetoothctl power off");
            break;
        case CC_DARK:
            cc->dark = !cc->dark;
            config_write_theme_dark(cc->dark);
            break;
        case CC_MUTE:
            cc->mute = !cc->mute;
            cc_exec(cc->mute
                        ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ 1 2>/dev/null || "
                          "pactl set-sink-mute @DEFAULT_SINK@ 1 2>/dev/null || amixer set Master mute"
                        : "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0 2>/dev/null || "
                          "pactl set-sink-mute @DEFAULT_SINK@ 0 2>/dev/null || amixer set Master unmute");
            break;
        case CC_NIGHT:
            cc->night = !cc->night;
            config_write_theme_night(cc->night);
            break;
            case CC_POWER:
            cc->power = (cc->power + 1) % 3;
            cc_exec(cc->power == 1 ? "powerprofilesctl set performance 2>/dev/null"
                    : cc->power == 2 ? "powerprofilesctl set power-saver 2>/dev/null"
                                     : "powerprofilesctl set balanced 2>/dev/null");
            break;
        case CC_BAR_WEA:
            cc->bar_weather = !cc->bar_weather;
            config_write_bar_show_weather(cc->bar_weather);
            break;
        case CC_WP_DIR:
            cc_cycle_wallpaper_dir(cc);
            break;
        default: break;
    }
}

static void cc_set_volume(struct control_center *cc, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    cc->volume = pct;
    char cmd[160];
    snprintf(cmd, sizeof(cmd),
             "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% 2>/dev/null || "
             "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null || "
             "amixer set Master %d%%", pct, pct, pct);
    cc_exec(cmd);
}

static void cc_set_brightness(struct control_center *cc, int pct)
{
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    cc->brightness = pct;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "brightnessctl -c backlight set %d%% 2>/dev/null", pct);
    cc_exec(cmd);
}

static double cc_content_top(void)
{
    return CC_PAD + CC_TITLE_H + CC_GAP + CC_TAB_H + CC_GAP;
}

static void cc_tab_geom(int w, double *tx, double *tw)
{
    double avail = w - 2 * CC_PAD - (TAB_MAX - 1) * CC_TAB_GAP;
    *tw = avail / TAB_MAX;
    for (int i = 0; i < TAB_MAX; i++)
        tx[i] = CC_PAD + i * (*tw + CC_TAB_GAP);
}

static void cc_layout(int w, double top, struct cc_geom *g)
{
    double y = top;
    double grid_w = w - 2 * CC_PAD;
    g->tile_w = (grid_w - CC_TILE_GAP) / 2.0;
    g->tile_h = CC_TILE_H;
    for (int i = 0; i < CC_TOGGLE_MAX; i++) {
        int col = i % 2;
        int row = i / 2;
        g->tile_x[i] = CC_PAD + col * (g->tile_w + CC_TILE_GAP);
        g->tile_y[i] = y + row * (CC_TILE_H + CC_TILE_GAP);
    }
    int rows = (CC_TOGGLE_MAX + 1) / 2;
    double tiles_bottom = y + rows * CC_TILE_H + (rows - 1) * CC_TILE_GAP;
    y = tiles_bottom + CC_GAP;

    g->wp_lbl_y = y;
    g->wp_btn_y = y + 20;
    g->wp_btn_h = 34;
    y = g->wp_btn_y + g->wp_btn_h + CC_GAP;

    g->vol_x = CC_PAD;
    g->vol_y = y;
    g->vol_w = grid_w;
    g->vol_h = CC_SLIDER_H;
    y += CC_SLIDER_H + CC_SLIDER_GAP;
    g->bri_x = CC_PAD;
    g->bri_y = y;
    g->bri_w = grid_w;
    g->bri_h = CC_SLIDER_H;
    y += CC_SLIDER_H + CC_GAP;

    g->pwr_w = (grid_w - (CC_PWR_N - 1) * CC_POWER_GAP) / CC_PWR_N;
    g->pwr_h = CC_POWER_H;
    g->pwr_y = y;
    for (int i = 0; i < CC_PWR_N; i++)
        g->pwr_x[i] = CC_PAD + i * (g->pwr_w + CC_POWER_GAP);
    y += CC_POWER_H + CC_GAP;

    g->wth_x = CC_PAD;
    g->wth_w = grid_w;
    g->wth_y = y;
    g->wth_h = CC_H - y - CC_PAD;
    if (g->wth_h < 64) g->wth_h = 64;
}

static void set_layout_font(PangoLayout *l, const char *family, double size, PangoWeight weight)
{
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(fd, weight);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
}

static void draw_text(cairo_t *cr, PangoLayout *l, const char *txt, double x, double y)
{
    pango_layout_set_text(l, txt, -1);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, l);
}

static double text_w(PangoLayout *l, const char *txt)
{
    int w, h;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &w, &h);
    return w;
}

static void text_size(PangoLayout *l, const char *txt, double *w, double *h)
{
    int iw, ih;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &iw, &ih);
    if (w) *w = iw;
    if (h) *h = ih;
}

static const char *cc_toggle_name(int idx)
{
    switch (idx) {
        case CC_WIFI: return "Wi-Fi";
        case CC_BT: return "Bluetooth";
        case CC_DARK: return "Dark mode";
        case CC_MUTE: return "Mute";
        case CC_NIGHT: return "Night light";
        case CC_POWER: return "Power profile";
        case CC_BAR_WEA: return "Clima en barra";
        case CC_WP_DIR: return "Carpeta";
        default: return "";
    }
}

static bool cc_toggle_on(const struct control_center *cc, int idx)
{
    switch (idx) {
        case CC_WIFI: return cc->wifi;
        case CC_BT: return cc->bt;
        case CC_DARK: return cc->dark;
        case CC_MUTE: return cc->mute;
        case CC_NIGHT: return cc->night;
        case CC_POWER: return cc->power != 0;
        case CC_BAR_WEA: return cc->bar_weather;
        case CC_WP_DIR: return cc->wp_dir[0] != '\0';
        default: return false;
    }
}

static const char *cc_toggle_state(const struct control_center *cc, int idx, char *buf, size_t n)
{
    switch (idx) {
        case CC_WIFI: return cc->wifi ? "On" : "Off";
        case CC_BT: return cc->bt ? "On" : "Off";
        case CC_DARK: return cc->dark ? "On" : "Off";
        case CC_MUTE: return cc->mute ? "Muted" : "On";
        case CC_NIGHT: return cc->night ? "On" : "Off";
        case CC_POWER:
            snprintf(buf, n, "%s", cc->power == 1 ? "Performance"
                                  : cc->power == 2 ? "Power saver" : "Balanced");
            return buf;
        case CC_BAR_WEA: return cc->bar_weather ? "On" : "Off";
        case CC_WP_DIR: return cc_dir_label(cc, buf, n);
        default: return "";
    }
}

static const char *cc_power_glyph(int idx)
{
    switch (idx) {
        case PWR_LOCK: return "L";
        case PWR_SUSPEND: return "Z";
        case PWR_LOGOUT: return "O";
        case PWR_REBOOT: return "R";
        case PWR_OFF: return "P";
        default: return "";
    }
}

static const char *cc_power_label(int idx)
{
    switch (idx) {
        case PWR_LOCK: return "Lock";
        case PWR_SUSPEND: return "Sleep";
        case PWR_LOGOUT: return "Logout";
        case PWR_REBOOT: return "Reboot";
        case PWR_OFF: return "Off";
        default: return "";
    }
}

static void cc_draw_slider(cairo_t *cr, const struct theme_config *th, PangoLayout *l_small,
                           PangoLayout *l_body, double x, double y, double w, const char *label,
                           int value, bool hover)
{
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.9);
    draw_text(cr, l_small, label, x, y);

    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", value);
    double pw = text_w(l_body, pct);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.9);
    draw_text(cr, l_body, pct, x + w - pw, y - 2);

    double ty = y + 24;
    double th_px = 8;
    rounded_rect(cr, x, ty, w, th_px, th_px / 2);
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                          th->surface_variant.b, hover ? 1.0 : 0.85);
    cairo_fill(cr);

    double frac = value / 100.0;
    if (frac > 0) {
        rounded_rect(cr, x, ty, w * frac, th_px, th_px / 2);
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        cairo_fill(cr);
    }
    double kx = x + w * frac;
    cairo_arc(cr, kx, ty + th_px / 2, hover ? 8.0 : 7.0, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_fill(cr);
    cairo_arc(cr, kx, ty + th_px / 2, hover ? 5.0 : 4.0, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
    cairo_fill(cr);
}

/* ---- tab bar ------------------------------------------------------------ */

static void cc_draw_tabbar(cairo_t *cr, struct control_center *cc, const struct theme_config *th,
                           int w)
{
    double tx[TAB_MAX], tw;
    cc_tab_geom(w, tx, &tw);
    double ty = CC_PAD + CC_TITLE_H + CC_GAP;
    for (int i = 0; i < TAB_MAX; i++) {
        bool active = (cc->tab == i);
        bool hov = (cc->hover == CC_HOVER_TAB_BASE + i);
        rounded_rect(cr, tx[i], ty, tw, CC_TAB_H, 9);
        if (active)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, hov ? 1.0 : 0.85);
        cairo_fill(cr);
        struct color_rgba ink = active ? th->on_primary : th->on_surface_variant;
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.95);
        const char *name = cc_tab_name(i);
        double nw = text_w(cc->l_small, name);
        draw_text(cr, cc->l_small, name, tx[i] + (tw - nw) / 2, ty + 8);
    }
}

/* ---- home tab ----------------------------------------------------------- */

static void cc_draw_home(cairo_t *cr, struct control_center *cc, const struct theme_config *th,
                         const char *family, double fsize, int w)
{
    struct cc_geom g;
    cc_layout(w, cc_content_top(), &g);

    char sbuf[64];
    for (int i = 0; i < CC_TOGGLE_MAX; i++) {
        double x = g.tile_x[i], y = g.tile_y[i];
        bool on = cc_toggle_on(cc, i);
        bool hov = (cc->hover == i);
        rounded_rect(cr, x, y, g.tile_w, g.tile_h, 12);
        if (on)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, hov ? 1.0 : 0.9);
        cairo_fill(cr);

        struct color_rgba ink = on ? th->on_primary : th->on_surface_variant;
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.95);
        draw_text(cr, cc->l_body, cc_toggle_name(i), x + 12, y + 9);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.7);
        draw_text(cr, cc->l_small, cc_toggle_state(cc, i, sbuf, sizeof(sbuf)), x + 12, y + 31);

        double sw = 30, sh = 16;
        double sx = x + g.tile_w - 12 - sw;
        double sy = y + g.tile_h / 2 - sh / 2;
        rounded_rect(cr, sx, sy, sw, sh, sh / 2);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, on ? 0.35 : 0.25);
        cairo_fill(cr);
        double knobx = on ? sx + sw - sh / 2 : sx + sh / 2;
        cairo_arc(cr, knobx, sy + sh / 2, sh / 2 - 2, 0, 2 * G_PI);
        cairo_set_source_rgba(cr, on ? th->on_primary.r : th->foreground.r,
                              on ? th->on_primary.g : th->foreground.g,
                              on ? th->on_primary.b : th->foreground.b, 1.0);
        cairo_fill(cr);
    }

    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
    draw_text(cr, cc->l_body, "Wallpaper", CC_PAD, g.wp_lbl_y);
    double bwbtn = (w - 2 * CC_PAD - 2 * CC_TILE_GAP) / 3.0;
    const char *wlbl[3] = { "Prev", "Next", "Random" };
    for (int i = 0; i < 3; i++) {
        double bx = CC_PAD + i * (bwbtn + CC_TILE_GAP);
        rounded_rect(cr, bx, g.wp_btn_y, bwbtn, g.wp_btn_h, 10);
        cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                              th->surface_variant.b, 0.9);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        double tw = text_w(cc->l_body, wlbl[i]);
        draw_text(cr, cc->l_body, wlbl[i], bx + (bwbtn - tw) / 2, g.wp_btn_y + 8);
    }

    cc_draw_slider(cr, th, cc->l_small, cc->l_body, g.vol_x, g.vol_y + 2, g.vol_w, "Volume",
                   cc->volume, cc->hover == CC_HOVER_VOLUME);
    cc_draw_slider(cr, th, cc->l_small, cc->l_body, g.bri_x, g.bri_y, g.bri_w, "Brightness",
                   cc->brightness, cc->hover == CC_HOVER_BRIGHTNESS);

    for (int i = 0; i < CC_PWR_N; i++) {
        double x = g.pwr_x[i], y = g.pwr_y;
        bool hov = (cc->hover == CC_HOVER_POWER_BASE + i);
        rounded_rect(cr, x, y, g.pwr_w, g.pwr_h, 10);
        cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                              th->surface_variant.b, hov ? 1.0 : 0.9);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        const char *gl = cc_power_glyph(i);
        double gw = text_w(cc->l_body, gl);
        draw_text(cr, cc->l_body, gl, x + (g.pwr_w - gw) / 2, y + 4);
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.8);
        const char *lab = cc_power_label(i);
        double lw = text_w(cc->l_small, lab);
        draw_text(cr, cc->l_small, lab, x + (g.pwr_w - lw) / 2, y + 22);
    }

    if (cc->w_ok) {
        double wx = g.wth_x, wy = g.wth_y, ww = g.wth_w, wh = g.wth_h;
        double pad = 14.0;

        rounded_rect(cr, wx, wy, ww, wh, 14);
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.14);
        cairo_fill(cr);

        double d = wh - 2 * pad;
        if (d > 54) d = 54;
        if (d < 32) d = 32;
        double cx = wx + pad + d / 2, cy = wy + wh / 2;
        cairo_arc(cr, cx, cy, d / 2, 0, 2 * G_PI);
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.22);
        cairo_fill(cr);
        set_layout_font(cc->l_title, family, d * 0.46, PANGO_WEIGHT_NORMAL);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
        double iw = 0, ih = 0;
        text_size(cc->l_title, cc->w_icon, &iw, &ih);
        draw_text(cr, cc->l_title, cc->w_icon, cx - iw / 2, cy - ih / 2);

        double tx = wx + pad + d + 14;
        set_layout_font(cc->l_title, family, fsize + 9, PANGO_WEIGHT_SEMIBOLD);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
        double tw2 = 0, thh = 0;
        text_size(cc->l_title, cc->w_temp, &tw2, &thh);
        draw_text(cr, cc->l_title, cc->w_temp, tx, wy + pad - 2);
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.85);
        set_layout_font(cc->l_small, family, fsize, PANGO_WEIGHT_NORMAL);
        draw_text(cr, cc->l_small, cc->w_cond, tx, wy + pad - 2 + thh + 2);

        if (cc->w_moon[0]) {
            char moonline[64];
            snprintf(moonline, sizeof(moonline), "%s %s", cc_moon_icon(cc->w_moon_phase), cc->w_moon);
            set_layout_font(cc->l_small, family, fsize - 1, PANGO_WEIGHT_NORMAL);
            cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                                  th->on_surface_variant.b, 0.85);
            draw_text(cr, cc->l_small, moonline, tx, wy + wh - pad - fsize);
        }

        set_layout_font(cc->l_body, family, fsize, PANGO_WEIGHT_MEDIUM);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.9);
        double lw2 = text_w(cc->l_body, cc->w_loc);
        draw_text(cr, cc->l_body, cc->w_loc, wx + ww - pad - lw2, wy + pad - 2);
        if (cc->w_extra[0]) {
            set_layout_font(cc->l_small, family, fsize - 1, PANGO_WEIGHT_NORMAL);
            cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                                  th->on_surface_variant.b, 0.75);
            double ew = text_w(cc->l_small, cc->w_extra);
            draw_text(cr, cc->l_small, cc->w_extra, wx + ww - pad - ew, wy + wh - pad - fsize);
        }
    }
}

/* ---- notifications tab -------------------------------------------------- */

#define CC_NF_ROW_H 64.0
#define CC_NF_CLEAR_W 84.0
#define CC_NF_CLEAR_H 28.0

static void cc_draw_notifications(cairo_t *cr, struct control_center *cc,
                                  const struct theme_config *th, int w)
{
    const struct notification_store *s = cc->nstore;
    double top = cc_content_top();
    double cw = w - 2 * CC_PAD;

    int count = s ? s->count : 0;
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%d notificacion%s", count, count == 1 ? "" : "es");
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.9);
    draw_text(cr, cc->l_small, hdr, CC_PAD, top + 6);

    /* clear button */
    double cbx = w - CC_PAD - CC_NF_CLEAR_W;
    bool chov = (cc->hover == CC_HOVER_NF_CLEAR);
    rounded_rect(cr, cbx, top, CC_NF_CLEAR_W, CC_NF_CLEAR_H, 8);
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                          th->surface_variant.b, chov ? 1.0 : 0.85);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.9);
    double clw = text_w(cc->l_small, "Limpiar");
    draw_text(cr, cc->l_small, "Limpiar", cbx + (CC_NF_CLEAR_W - clw) / 2, top + 4);

    if (count == 0) {
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.6);
        draw_text(cr, cc->l_body, "Sin notificaciones", CC_PAD, top + 52);
        return;
    }

    pango_layout_set_width(cc->l_body, (int)((cw - 24) * PANGO_SCALE));
    pango_layout_set_ellipsize(cc->l_body, PANGO_ELLIPSIZE_END);
    pango_layout_set_width(cc->l_small, (int)((cw - 24) * PANGO_SCALE));
    pango_layout_set_ellipsize(cc->l_small, PANGO_ELLIPSIZE_END);

    double y = top + CC_NF_CLEAR_H + 10;
    double row_h = CC_NF_ROW_H;
    for (int i = count - 1; i >= 0; i--) {
        if (y + row_h > CC_H - CC_PAD) break;
        const struct notification *n = &s->items[i];
        rounded_rect(cr, CC_PAD, y, cw, row_h - 8, 12);
        cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                              th->surface_variant.b, n->read ? 0.55 : 0.85);
        cairo_fill(cr);
        if (!n->read) {
            rounded_rect(cr, CC_PAD + 8, y + 12, 4, 16, 2);
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
            cairo_fill(cr);
        }
        char when[16];
        struct tm tmv;
        localtime_r(&n->timestamp, &tmv);
        if (strftime(when, sizeof(when), "%H:%M", &tmv) == 0) when[0] = '\0';
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.95);
        draw_text(cr, cc->l_small, n->app_name[0] ? n->app_name : "Sistema", CC_PAD + 20, y + 6);
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.7);
        double ww = text_w(cc->l_small, when);
        draw_text(cr, cc->l_small, when, CC_PAD + cw - 14 - ww, y + 6);

        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        draw_text(cr, cc->l_body, n->summary[0] ? n->summary : "(sin titulo)", CC_PAD + 20, y + 24);
        if (n->body[0]) {
            cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                                  th->on_surface_variant.b, 0.8);
            draw_text(cr, cc->l_small, n->body, CC_PAD + 20, y + 42);
        }
        y += row_h;
    }

    pango_layout_set_width(cc->l_body, -1);
    pango_layout_set_ellipsize(cc->l_body, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_width(cc->l_small, -1);
    pango_layout_set_ellipsize(cc->l_small, PANGO_ELLIPSIZE_NONE);
}

/* ---- shortcuts tab ------------------------------------------------------ */

static void cc_draw_shortcuts(cairo_t *cr, struct control_center *cc,
                              const struct theme_config *th, int w)
{
    double top = cc_content_top();
    double cw = w - 2 * CC_PAD;
    double tw = (cw - CC_TILE_GAP) / 2.0;
    double th_tile = 74.0;
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.9);
    draw_text(cr, cc->l_small, "Atajos rapidos", CC_PAD, top + 6);
    double y0 = top + 20;
    for (int i = 0; i < CC_SC_N; i++) {
        int col = i % 2, row = i / 2;
        double x = CC_PAD + col * (tw + CC_TILE_GAP);
        double y = y0 + row * (th_tile + CC_TILE_GAP);
        bool hov = (cc->hover == CC_HOVER_SC_BASE + i);
        rounded_rect(cr, x, y, tw, th_tile, 12);
        cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                              th->surface_variant.b, hov ? 1.0 : 0.9);
        cairo_fill(cr);
        double d = 40.0;
        double cx = x + d / 2 + 12, cy = y + th_tile / 2;
        cairo_arc(cr, cx, cy, d / 2, 0, 2 * G_PI);
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, hov ? 1.0 : 0.85);
        cairo_fill(cr);
        set_layout_font(cc->l_body, (cc->cfg && cc->cfg->font.family[0]) ? cc->cfg->font.family : "sans-serif",
                        cc->cfg && cc->cfg->font.size > 0 ? cc->cfg->font.size + 4 : 15,
                        PANGO_WEIGHT_SEMIBOLD);
        cairo_set_source_rgba(cr, th->on_primary.r, th->on_primary.g, th->on_primary.b, 0.95);
        double gw = text_w(cc->l_body, cc_shortcuts[i].glyph);
        draw_text(cr, cc->l_body, cc_shortcuts[i].glyph, cx - gw / 2, cy - 10);
        set_layout_font(cc->l_body, (cc->cfg && cc->cfg->font.family[0]) ? cc->cfg->font.family : "sans-serif",
                        cc->cfg && cc->cfg->font.size > 0 ? cc->cfg->font.size : 11,
                        PANGO_WEIGHT_MEDIUM);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        draw_text(cr, cc->l_body, cc_shortcuts[i].label, x + d + 24, y + th_tile / 2 - 10);
    }
}

/* ---- screen tab --------------------------------------------------------- */

static void cc_draw_screen(cairo_t *cr, struct control_center *cc,
                           const struct theme_config *th, int w)
{
    double top = cc_content_top();
    double cw = w - 2 * CC_PAD;

    /* master switch for the screen lock */
    bool lhov = (cc->hover == CC_HOVER_LOCK);
    rounded_rect(cr, CC_PAD, top, cw, CC_LOCK_H, 12);
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                          th->surface_variant.b, lhov ? 1.0 : 0.9);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
    draw_text(cr, cc->l_body, "Bloqueo de pantalla", CC_PAD + 12, top + 8);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.7);
    draw_text(cr, cc->l_small, cc->lock_enabled ? "On" : "Off", CC_PAD + 12, top + 30);
    {
        double sw = 30, sh = 16;
        double sx = CC_PAD + cw - 12 - sw;
        double sy = top + CC_LOCK_H / 2 - sh / 2;
        struct color_rgba on_col = th->primary;
        struct color_rgba off_col = th->foreground;
        if (cc->lock_enabled) {
            rounded_rect(cr, sx, sy, sw, sh, sh / 2);
            cairo_set_source_rgba(cr, on_col.r, on_col.g, on_col.b, 0.9);
            cairo_fill(cr);
        } else {
            rounded_rect(cr, sx, sy, sw, sh, sh / 2);
            cairo_set_source_rgba(cr, off_col.r, off_col.g, off_col.b, 0.22);
            cairo_fill(cr);
        }
        double knobx = cc->lock_enabled ? sx + sw - sh / 2 : sx + sh / 2;
        cairo_arc(cr, knobx, sy + sh / 2, sh / 2 - 2, 0, 2 * G_PI);
        if (cc->lock_enabled)
            cairo_set_source_rgba(cr, th->on_primary.r, th->on_primary.g, th->on_primary.b, 1.0);
        else
            cairo_set_source_rgba(cr, off_col.r, off_col.g, off_col.b, 1.0);
        cairo_fill(cr);
    }
    top += CC_LOCK_H + CC_GAP;

    long secs = (long)(time(NULL) - cc->start_time);
    if (secs < 0) secs = 0;
    char upt[32];
    if (secs >= 3600) snprintf(upt, sizeof(upt), "%ld h %02ld m", secs / 3600, (secs % 3600) / 60);
    else snprintf(upt, sizeof(upt), "%ld m %02ld s", secs / 60, secs % 60);

    int nwin = cc->niri ? niri_ipc_window_count(cc->niri) : 0;
    char hdr[80];
    snprintf(hdr, sizeof(hdr), "Sesion %s", upt);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
    draw_text(cr, cc->l_body, hdr, CC_PAD, top + 6);
    char sub[64];
    snprintf(sub, sizeof(sub), "%d ventana%s", nwin, nwin == 1 ? "" : "s");
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.8);
    double sw = text_w(cc->l_small, sub);
    draw_text(cr, cc->l_small, sub, w - CC_PAD - sw, top + 8);

    rounded_rect(cr, CC_PAD, top + 28, cw, 10, 5);
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                          th->surface_variant.b, 0.8);
    cairo_fill(cr);
    double frac = (secs % 3600) / 3600.0;
    if (frac < 0.02) frac = 0.02;
    rounded_rect(cr, CC_PAD, top + 28, cw * frac, 10, 5);
    cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
    cairo_fill(cr);

    double y = top + 52;
    pango_layout_set_width(cc->l_body, (int)((cw - 24) * PANGO_SCALE));
    pango_layout_set_ellipsize(cc->l_body, PANGO_ELLIPSIZE_END);
    if (!cc->niri || nwin == 0) {
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.6);
        draw_text(cr, cc->l_body, cc->niri ? "Sin ventanas abiertas" : "Compositor no disponible",
                  CC_PAD, y + 4);
        pango_layout_set_width(cc->l_body, -1);
        pango_layout_set_ellipsize(cc->l_body, PANGO_ELLIPSIZE_NONE);
        return;
    }
    double row_h = 46.0;
    for (int i = 0; i < nwin; i++) {
        if (y + row_h > CC_H - CC_PAD) break;
        const struct niri_window *win = niri_ipc_window(cc->niri, i);
        if (!win) continue;
        bool hov = (cc->hover == CC_HOVER_WIN_BASE + i);
        rounded_rect(cr, CC_PAD, y, cw, row_h - 8, 10);
        if (win->focused)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.28);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, hov ? 1.0 : 0.8);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        draw_text(cr, cc->l_body, win->app_id[0] ? win->app_id : "(app)", CC_PAD + 14, y + 4);
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.8);
        draw_text(cr, cc->l_small, win->title[0] ? win->title : "", CC_PAD + 14, y + 22);
        y += row_h;
    }
    pango_layout_set_width(cc->l_body, -1);
    pango_layout_set_ellipsize(cc->l_body, PANGO_ELLIPSIZE_NONE);
}

/* ---- panel callbacks ---------------------------------------------------- */

static void cc_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    (void)h;
    struct control_center *cc = p->userdata;
    struct config def;
    config_defaults(&def);
    const struct theme_config *th = cc->cfg ? &cc->cfg->theme : &def.theme;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!cc->l_title) {
        cc->l_title = pango_cairo_create_layout(cr);
        cc->l_body = pango_cairo_create_layout(cr);
        cc->l_small = pango_cairo_create_layout(cr);
    }
    const char *family = (cc->cfg && cc->cfg->font.family[0]) ? cc->cfg->font.family : "sans-serif";
    double fsize = (cc->cfg && cc->cfg->font.size > 0) ? cc->cfg->font.size : 11.0;
    set_layout_font(cc->l_title, family, fsize + 4, PANGO_WEIGHT_MEDIUM);
    set_layout_font(cc->l_body, family, fsize, PANGO_WEIGHT_MEDIUM);
    set_layout_font(cc->l_small, family, fsize - 1, PANGO_WEIGHT_NORMAL);

    /* header: title on the left, today's date on the right */
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    draw_text(cr, cc->l_title, "Control Center", CC_PAD, CC_PAD);
    char datebuf[64];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (strftime(datebuf, sizeof(datebuf), "%a %d %b", &tmv) == 0) datebuf[0] = '\0';
    double dw = text_w(cc->l_small, datebuf);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.85);
    draw_text(cr, cc->l_small, datebuf, w - CC_PAD - dw, CC_PAD + 8);

    cc_draw_tabbar(cr, cc, th, w);

    switch (cc->tab) {
        case TAB_NOTIF: cc_draw_notifications(cr, cc, th, w); break;
        case TAB_SHORTCUTS: cc_draw_shortcuts(cr, cc, th, w); break;
        case TAB_SCREEN: cc_draw_screen(cr, cc, th, w); break;
        case TAB_HOME:
        default: cc_draw_home(cr, cc, th, family, fsize, w); break;
    }
}

static void cc_power_action(struct control_center *cc, int idx)
{
    switch (idx) {
        case PWR_LOCK:
            if (cc->lock) lock_screen_lock(cc->lock);
            panel_hide(&cc->panel);
            break;
        case PWR_SUSPEND:
            if (cc->sa) session_actions_suspend(cc->sa);
            panel_hide(&cc->panel);
            break;
        case PWR_LOGOUT:
            if (cc->sa) session_actions_logout(cc->sa);
            break;
        case PWR_REBOOT:
            if (cc->sa) session_actions_reboot(cc->sa);
            break;
        case PWR_OFF:
            if (cc->sa) session_actions_poweroff(cc->sa);
            break;
        default: break;
    }
}

static void cc_run_shortcut(struct control_center *cc, int i)
{
    if (i < 0 || i >= CC_SC_N) return;
    const char *cmd = cc_shortcuts[i].cmd;
    if (cmd[0] == '@') {
        const char *id = cmd + 1;
        if (strcmp(id, "lock") == 0) {
            if (cc->lock) lock_screen_lock(cc->lock);
            panel_hide(&cc->panel);
        } else if (cc->shortcut_fn) {
            cc->shortcut_fn(cc->shortcut_ud, id);
        }
    } else {
        cc_exec(cmd);
    }
}

static bool cc_click(struct panel *p, double x, double y)
{
    struct control_center *cc = p->userdata;
    int w = p->logical_w;

    /* tab bar */
    {
        double tx[TAB_MAX], tw;
        cc_tab_geom(w, tx, &tw);
        double ty = CC_PAD + CC_TITLE_H + CC_GAP;
        if (y >= ty && y < ty + CC_TAB_H) {
            for (int i = 0; i < TAB_MAX; i++) {
                if (x >= tx[i] && x < tx[i] + tw) {
                    cc->tab = i;
                    panel_mark_dirty(p);
                    return true;
                }
            }
        }
    }

    if (cc->tab == TAB_NOTIF) {
        double top = cc_content_top();
        if (y >= top && y < top + CC_NF_CLEAR_H &&
            x >= w - CC_PAD - CC_NF_CLEAR_W && x < w - CC_PAD) {
            struct notification_store *s = (struct notification_store *)cc->nstore;
            notify_store_clear(s);
            cc->last_ncount = 0;
            panel_mark_dirty(p);
            return true;
        }
        if (cc->nstore) {
            double cw = w - 2 * CC_PAD;
            double yy = top + CC_NF_CLEAR_H + 10;
            for (int i = cc->nstore->count - 1; i >= 0; i--) {
                if (yy + CC_NF_ROW_H > CC_H - CC_PAD) break;
                if (x >= CC_PAD && x < CC_PAD + cw && y >= yy && y < yy + CC_NF_ROW_H - 8) {
                    notify_store_mark_read((struct notification_store *)cc->nstore, i);
                    panel_mark_dirty(p);
                    return true;
                }
                yy += CC_NF_ROW_H;
            }
        }
        return true;
    }

    if (cc->tab == TAB_SHORTCUTS) {
        double top = cc_content_top();
        double cw = w - 2 * CC_PAD;
        double tw = (cw - CC_TILE_GAP) / 2.0;
        double th_tile = 74.0;
        double y0 = top + 20;
        for (int i = 0; i < CC_SC_N; i++) {
            int col = i % 2, row = i / 2;
            double bx = CC_PAD + col * (tw + CC_TILE_GAP);
            double by = y0 + row * (th_tile + CC_TILE_GAP);
            if (x >= bx && x < bx + tw && y >= by && y < by + th_tile) {
                cc_run_shortcut(cc, i);
                return true;
            }
        }
        return true;
    }

    if (cc->tab == TAB_SCREEN) {
        {
            double top = cc_content_top();
            if (y >= top && y < top + CC_LOCK_H && x >= CC_PAD && x < w - CC_PAD) {
                cc->lock_enabled = !cc->lock_enabled;
                config_write_lock_enabled(cc->lock_enabled);
                panel_mark_dirty(p);
                return true;
            }
        }
        if (cc->niri) {
            double top = cc_content_top() + CC_LOCK_H + CC_GAP;
            double yy = top + 52;
            int nwin = niri_ipc_window_count(cc->niri);
            for (int i = 0; i < nwin; i++) {
                if (yy + 46 > CC_H - CC_PAD) break;
                if (x >= CC_PAD && x < w - CC_PAD && y >= yy && y < yy + 46 - 8) {
                    const struct niri_window *win = niri_ipc_window(cc->niri, i);
                    if (win) niri_ipc_focus(cc->niri, win->id);
                    panel_mark_dirty(p);
                    return true;
                }
                yy += 46;
            }
        }
        return true;
    }

    /* home tab */
    struct cc_geom g;
    cc_layout(w, cc_content_top(), &g);

    for (int i = 0; i < CC_TOGGLE_MAX; i++) {
        if (x >= g.tile_x[i] && x < g.tile_x[i] + g.tile_w &&
            y >= g.tile_y[i] && y < g.tile_y[i] + g.tile_h) {
            cc_apply_toggle(cc, i);
            panel_mark_dirty(p);
            return true;
        }
    }
    {
        double bwbtn = (w - 2 * CC_PAD - 2 * CC_TILE_GAP) / 3.0;
        for (int i = 0; i < 3; i++) {
            double bx = CC_PAD + i * (bwbtn + CC_TILE_GAP);
            if (y >= g.wp_btn_y && y < g.wp_btn_y + g.wp_btn_h && x >= bx && x < bx + bwbtn) {
                struct wallpaper *wp = wallpaper_instance_get();
                if (wp) { if (i == 0) wallpaper_prev(wp); else if (i == 1) wallpaper_next(wp); else wallpaper_random(wp); }
                panel_mark_dirty(p);
                return true;
            }
        }
    }
    if (y >= g.vol_y && y < g.vol_y + g.vol_h) {
        cc_set_volume(cc, (int)((x - g.vol_x) / g.vol_w * 100.0 + 0.5));
        panel_mark_dirty(p);
        return true;
    }
    if (y >= g.bri_y && y < g.bri_y + g.bri_h) {
        cc_set_brightness(cc, (int)((x - g.bri_x) / g.bri_w * 100.0 + 0.5));
        panel_mark_dirty(p);
        return true;
    }
    for (int i = 0; i < CC_PWR_N; i++) {
        if (x >= g.pwr_x[i] && x < g.pwr_x[i] + g.pwr_w &&
            y >= g.pwr_y && y < g.pwr_y + g.pwr_h) {
            cc_power_action(cc, i);
            return true;
        }
    }
    return true;
}

static void cc_motion(struct panel *p, double x, double y)
{
    struct control_center *cc = p->userdata;
    int w = p->logical_w;
    int h = CC_HOVER_NONE;

    /* tab bar first */
    {
        double tx[TAB_MAX], tw;
        cc_tab_geom(w, tx, &tw);
        double ty = CC_PAD + CC_TITLE_H + CC_GAP;
        if (y >= ty && y < ty + CC_TAB_H) {
            for (int i = 0; i < TAB_MAX; i++) {
                if (x >= tx[i] && x < tx[i] + tw) { h = CC_HOVER_TAB_BASE + i; break; }
            }
            goto done;
        }
    }

    if (cc->tab == TAB_NOTIF) {
        double top = cc_content_top();
        if (y >= top && y < top + CC_NF_CLEAR_H && x >= w - CC_PAD - CC_NF_CLEAR_W)
            h = CC_HOVER_NF_CLEAR;
        goto done;
    }
    if (cc->tab == TAB_SHORTCUTS) {
        double top = cc_content_top();
        double cw = w - 2 * CC_PAD;
        double tw = (cw - CC_TILE_GAP) / 2.0;
        double th_tile = 74.0, y0 = top + 20;
        for (int i = 0; i < CC_SC_N; i++) {
            int col = i % 2, row = i / 2;
            double bx = CC_PAD + col * (tw + CC_TILE_GAP);
            double by = y0 + row * (th_tile + CC_TILE_GAP);
            if (x >= bx && x < bx + tw && y >= by && y < by + th_tile) {
                h = CC_HOVER_SC_BASE + i; break;
            }
        }
        goto done;
    }
    if (cc->tab == TAB_SCREEN) {
        {
            double top = cc_content_top();
            if (y >= top && y < top + CC_LOCK_H && x >= CC_PAD && x < w - CC_PAD) {
                h = CC_HOVER_LOCK;
                goto done;
            }
        }
        if (cc->niri) {
            double top = cc_content_top() + CC_LOCK_H + CC_GAP;
            double yy = top + 52;
            int nwin = niri_ipc_window_count(cc->niri);
            for (int i = 0; i < nwin; i++) {
                if (yy + 46 > CC_H - CC_PAD) break;
                if (x >= CC_PAD && x < w - CC_PAD && y >= yy && y < yy + 46 - 8) {
                    h = CC_HOVER_WIN_BASE + i; break;
                }
                yy += 46;
            }
        }
        goto done;
    }

    /* home tab */
    {
        struct cc_geom g;
        cc_layout(w, cc_content_top(), &g);
        for (int i = 0; i < CC_TOGGLE_MAX; i++) {
            if (x >= g.tile_x[i] && x < g.tile_x[i] + g.tile_w &&
                y >= g.tile_y[i] && y < g.tile_y[i] + g.tile_h) {
                h = i;
                goto done;
            }
        }
        if (y >= g.vol_y && y < g.vol_y + g.vol_h) h = CC_HOVER_VOLUME;
        else if (y >= g.bri_y && y < g.bri_y + g.bri_h) h = CC_HOVER_BRIGHTNESS;
        else {
            for (int i = 0; i < CC_PWR_N; i++) {
                if (x >= g.pwr_x[i] && x < g.pwr_x[i] + g.pwr_w &&
                    y >= g.pwr_y && y < g.pwr_y + g.pwr_h) {
                    h = CC_HOVER_POWER_BASE + i;
                    break;
                }
            }
        }
    }
done:
    if (h != cc->hover) {
        cc->hover = h;
        panel_mark_dirty(p);
    }
}

static bool cc_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)mods;
    if (keysym == XKB_KEY_Escape) {
        panel_hide(p);
        return true;
    }
    return false;
}

struct control_center *control_center_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out)
{
    struct control_center *cc = calloc(1, sizeof(*cc));
    if (!cc) return NULL;
    panel_init(&cc->panel, ctx);
    cc->cfg = cfg ? *cfg : NULL;
    cc->fallback_out = out;
    cc->volume = 50;
    cc->brightness = 50;
    cc->hover = CC_HOVER_NONE;
    cc->tab = TAB_HOME;
    cc->start_time = time(NULL);
    panel_set_callbacks(&cc->panel, cc_draw, cc_key, cc_click, cc_motion, cc);
    if (cc->cfg) {
        cc->bar_weather = cc->cfg->bar.show_weather;
        snprintf(cc->wp_dir, sizeof(cc->wp_dir), "%s", cc->cfg->wallpaper.dir);
        cc->dark = cc->cfg->theme.dark;
        cc->night = cc->cfg->theme.night;
        cc->lock_enabled = cc->cfg->lock.enabled;
    }
    log_info("control center created");
    return cc;
}

void control_center_set_actions(struct control_center *cc, struct session_actions *sa,
                                struct lock_screen *lock)
{
    if (!cc) return;
    cc->sa = sa;
    cc->lock = lock;
}

void control_center_set_notifications(struct control_center *cc, struct notification_store *store)
{
    if (cc) cc->nstore = store;
}

void control_center_set_tab(struct control_center *cc, int tab)
{
    if (!cc) return;
    if (tab < 0 || tab >= TAB_MAX) return;
    cc->tab = tab;
    if (panel_is_visible(&cc->panel)) panel_mark_dirty(&cc->panel);
}

void control_center_set_niri(struct control_center *cc, struct niri_ipc *niri)
{
    if (cc) cc->niri = niri;
}

void control_center_set_shortcut_handler(struct control_center *cc,
                                         void (*fn)(void *userdata, const char *id),
                                         void *userdata)
{
    if (!cc) return;
    cc->shortcut_fn = fn;
    cc->shortcut_ud = userdata;
}

void control_center_destroy(struct control_center *cc)
{
    if (!cc) return;
    if (cc->l_title) g_object_unref(cc->l_title);
    if (cc->l_body) g_object_unref(cc->l_body);
    if (cc->l_small) g_object_unref(cc->l_small);
    panel_fini(&cc->panel);
    free(cc);
}

struct panel *control_center_surface(struct control_center *cc)
{
    return cc ? &cc->panel : NULL;
}

void control_center_toggle_proxy(void *userdata)
{
    struct control_center *cc = userdata;
    if (!cc) return;
    if (panel_is_visible(&cc->panel)) {
        panel_hide(&cc->panel);
        return;
    }
    cc_query_state(cc);
    cc_weather_load(cc);
    time_t nowt = time(NULL);
    if (nowt - cc->w_fetched > CC_WEATHER_TTL) {
        cc->w_fetched = nowt;
        cc_weather_refresh();
    }
    if (cc->cfg) {
        cc->bar_weather = cc->cfg->bar.show_weather;
        snprintf(cc->wp_dir, sizeof(cc->wp_dir), "%s", cc->cfg->wallpaper.dir);
        cc->dark = cc->cfg->theme.dark;
        cc->night = cc->cfg->theme.night;
        cc->lock_enabled = cc->cfg->lock.enabled;
    }
    cc->last_ncount = cc->nstore ? cc->nstore->count : 0;
    cc->hover = CC_HOVER_NONE;
    struct output *out = cc->panel.out ? cc->panel.out : cc->fallback_out;
    if (!panel_show_at(&cc->panel, out, CC_W, CC_H, PANEL_ANCHOR_TOP_RIGHT))
        log_warn("control center: show failed");
}

void control_center_tick(struct control_center *cc)
{
    if (!cc) return;
    struct stat st;
    if (stat(CC_WEATHER_CACHE, &st) == 0 && st.st_mtime != cc->w_mtime) {
        cc_weather_load(cc);
        if (panel_is_visible(&cc->panel)) panel_mark_dirty(&cc->panel);
    }
    time_t nowt = time(NULL);
    if (cc->w_fetched == 0 || nowt - cc->w_fetched > CC_WEATHER_TTL) {
        cc->w_fetched = nowt;
        cc_weather_refresh();
    }
    int nc = cc->nstore ? cc->nstore->count : 0;
    if (nc != cc->last_ncount) {
        cc->last_ncount = nc;
        if (panel_is_visible(&cc->panel) && cc->tab == TAB_NOTIF)
            panel_mark_dirty(&cc->panel);
    }
}
