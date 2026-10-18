#include "config/parser.h"
#include "config/config.h"
#include "util/log.h"
#include "third_party/tomlc99/toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <stdarg.h>
#include <stddef.h>

static void set_err(char *errbuf, size_t errbufsz, const char *fmt, ...)
{
    if (!errbuf || errbufsz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, errbufsz, fmt, ap);
    va_end(ap);
}
static const char *config_user_path(void)
{
    static char buf[512];
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home || home[0] == '\0') {
        return "config/config.toml";
    }
    snprintf(buf, sizeof(buf), "%s/.config/emancipation-shell/config.toml", home);
    return buf;
}

/* Path the shell loads config from: the user's file if present, otherwise a
 * system-wide default (useful for live/installed images). Writes always target
 * the user's file (see config_user_path). */
const char *config_default_path(void)
{
    const char *user = config_user_path();
    struct stat st;
    if (stat(user, &st) == 0) return user;
    static const char sys[] = "/etc/emancipation-shell/config.toml";
    if (stat(sys, &st) == 0) return sys;
    return user;
}

/* Best-effort creation of the parent directory of a path (one level). */
static void ensure_parent_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return;
    char dir[512];
    size_t n = (size_t)(slash - path);
    if (n >= sizeof(dir)) return;
    memcpy(dir, path, n);
    dir[n] = '\0';
    mkdir(dir, 0755);
}

static int read_file_all(const char *path, char **out, size_t *outlen)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(sz > 0 ? sz + 1 : 1);
    if (!data) { fclose(f); return -2; }
    size_t n = fread(data, 1, sz > 0 ? sz : 0, f);
    data[n] = '\0';
    fclose(f);
    *out = data;
    *outlen = n;
    return 0;
}

static bool get_double(toml_table_t *tbl, const char *key, double *v)
{
    toml_datum_t d = toml_double_in(tbl, key);
    if (!d.ok) return false;
    *v = d.u.d;
    return true;
}

static bool get_int(toml_table_t *tbl, const char *key, int *v)
{
    toml_datum_t d = toml_int_in(tbl, key);
    if (!d.ok) return false;
    *v = (int)d.u.i;
    return true;
}

static bool get_bool(toml_table_t *tbl, const char *key, bool *v)
{
    toml_datum_t d = toml_bool_in(tbl, key);
    if (!d.ok) return false;
    *v = d.u.b;
    return true;
}

static bool get_string(toml_table_t *tbl, const char *key, char *out, size_t outsz)
{
    toml_datum_t d = toml_string_in(tbl, key);
    if (!d.ok) return false;
    strncpy(out, d.u.s, outsz-1);
    out[outsz-1] = '\0';
    free(d.u.s);
    return true;
}

bool config_load_from_file(const char *path, struct config *out, char *errbuf, size_t errbufsz)
{
    if (!path || !out) {
        set_err(errbuf, errbufsz, "invalid args");
        return false;
    }
    struct config tmp;
    config_defaults(&tmp);
    FILE *f = fopen(path, "r");
    if (!f) {
        memcpy(out, &tmp, sizeof(tmp));
        return true;
    }
    fclose(f);
    char *data = NULL;
    size_t dlen = 0;
    if (read_file_all(path, &data, &dlen) < 0) {
        set_err(errbuf, errbufsz, "failed to read %s", path);
        return false;
    }
    char err[200];
    toml_table_t *tbl = toml_parse(data, err, sizeof(err));
    free(data);
    if (!tbl) {
        set_err(errbuf, errbufsz, "TOML parse error: %s", err);
        return false;
    }
    toml_table_t *bar = toml_table_in(tbl, "bar");
    if (bar) {
        get_int(bar, "height", &tmp.bar.height);
        get_bool(bar, "visible", &tmp.bar.visible);
        get_bool(bar, "show_weather", &tmp.bar.show_weather);
        get_int(bar, "padding", &tmp.bar.padding);
        get_int(bar, "spacing", &tmp.bar.spacing);
        get_string(bar, "clock_format", tmp.bar.clock_format, sizeof(tmp.bar.clock_format));
        get_string(bar, "date_format", tmp.bar.date_format, sizeof(tmp.bar.date_format));
        static const char *sections[BAR_SECTIONS] = { "start", "center", "end" };
        for (int s = 0; s < BAR_SECTIONS; s++) {
            toml_array_t *arr = toml_array_in(bar, sections[s]);
            if (!arr) continue;
            tmp.bar.n_modules[s] = 0;
            memset(tmp.bar.modules[s], 0, sizeof(tmp.bar.modules[s]));
            int n = toml_array_nelem(arr);
            for (int i = 0; i < n && tmp.bar.n_modules[s] < BAR_MAX_MODULES; i++) {
                toml_datum_t d = toml_string_at(arr, i);
                if (!d.ok) continue;
                strncpy(tmp.bar.modules[s][tmp.bar.n_modules[s]++], d.u.s, BAR_MODULE_NAME - 1);
                free(d.u.s);
            }
        }
        if (tmp.bar.height < 16) tmp.bar.height = 16;
        if (tmp.bar.height > 200) tmp.bar.height = 200;
    }
    toml_table_t *theme = toml_table_in(tbl, "theme");
    if (theme) {
        bool dark = true, night = false;
        get_bool(theme, "dark", &dark);
        get_bool(theme, "night", &night);
        if (!dark) theme_apply_palette(&tmp.theme, false);
        static const struct { const char *key; size_t off; } roles[] = {
            { "background", offsetof(struct theme_config, background) },
            { "panel_background", offsetof(struct theme_config, panel_background) },
            { "foreground", offsetof(struct theme_config, foreground) },
            { "primary", offsetof(struct theme_config, primary) },
            { "on_primary", offsetof(struct theme_config, on_primary) },
            { "secondary", offsetof(struct theme_config, secondary) },
            { "on_secondary", offsetof(struct theme_config, on_secondary) },
            { "error", offsetof(struct theme_config, error) },
            { "on_error", offsetof(struct theme_config, on_error) },
            { "surface_variant", offsetof(struct theme_config, surface_variant) },
            { "on_surface_variant", offsetof(struct theme_config, on_surface_variant) },
        };
        for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
            struct color_rgba *c = (struct color_rgba *)((char *)&tmp.theme + roles[i].off);
            char hex[16];
            if (get_string(theme, roles[i].key, hex, sizeof(hex))) {
                if (!color_from_hex(hex, c)) {
                    set_err(errbuf, errbufsz, "theme.%s: invalid color \"%s\"", roles[i].key, hex);
                    toml_free(tbl);
                    return false;
                }
                continue;
            }
            /* legacy form: [theme.<role>] r/g/b/a floats */
            toml_table_t *t = toml_table_in(theme, roles[i].key);
            if (t) {
                double v;
                if (get_double(t, "r", &v)) c->r = (float)v;
                if (get_double(t, "g", &v)) c->g = (float)v;
                if (get_double(t, "b", &v)) c->b = (float)v;
                if (get_double(t, "a", &v)) c->a = (float)v;
            }
        }
        tmp.theme.dark = dark;
        tmp.theme.night = night;
        if (night) theme_apply_night(&tmp.theme);
    }
    toml_table_t *font = toml_table_in(tbl, "font");
    if (font) {
        get_string(font, "family", tmp.font.family, sizeof(tmp.font.family));
        get_int(font, "size", &tmp.font.size);
    }
    toml_table_t *wp = toml_table_in(tbl, "wallpaper");
    if (wp) {
        get_string(wp, "path", tmp.wallpaper.path, sizeof(tmp.wallpaper.path));
        get_string(wp, "dir", tmp.wallpaper.dir, sizeof(tmp.wallpaper.dir));
        char f[32];
        if (get_string(wp, "fill", f, sizeof(f))) tmp.wallpaper.fill = wp_fill_from_str(f);
        get_bool(wp, "enabled", &tmp.wallpaper.enabled);
        get_int(wp, "interval", &tmp.wallpaper.interval);
        get_bool(wp, "randomize", &tmp.wallpaper.randomize);
        get_int(wp, "transition", &tmp.wallpaper.transition);
        if (tmp.wallpaper.interval < 0) tmp.wallpaper.interval = 0;
        if (tmp.wallpaper.transition < 0) tmp.wallpaper.transition = 0;
        if (tmp.wallpaper.transition > 5000) tmp.wallpaper.transition = 5000;
    }
    toml_table_t *idle = toml_table_in(tbl, "idle");
    if (idle) {
        get_bool(idle, "enabled", &tmp.idle.enabled);
        get_int(idle, "lock_timeout", &tmp.idle.lock_timeout);
        get_int(idle, "screen_off_timeout", &tmp.idle.screen_off_timeout);
        if (tmp.idle.lock_timeout < 0) tmp.idle.lock_timeout = 0;
        if (tmp.idle.screen_off_timeout < 0) tmp.idle.screen_off_timeout = 0;
    }
    toml_table_t *lock = toml_table_in(tbl, "lock");
    if (lock) {
        get_bool(lock, "enabled", &tmp.lock.enabled);
    }
    toml_table_t *dock = toml_table_in(tbl, "dock");
    if (dock) {
        get_bool(dock, "enabled", &tmp.dock.enabled);
        get_bool(dock, "autohide", &tmp.dock.autohide);
        get_int(dock, "height", &tmp.dock.height);
        get_int(dock, "icon_size", &tmp.dock.icon_size);
        if (tmp.dock.height < 24) tmp.dock.height = 24;
        if (tmp.dock.height > 200) tmp.dock.height = 200;
        if (tmp.dock.icon_size < 16) tmp.dock.icon_size = 16;
        if (tmp.dock.icon_size > tmp.dock.height) tmp.dock.icon_size = tmp.dock.height;
        toml_array_t *pinned = toml_array_in(dock, "pinned");
        if (pinned) {
            tmp.dock.n_pinned = 0;
            int n = toml_array_nelem(pinned);
            for (int i = 0; i < n && tmp.dock.n_pinned < DOCK_MAX_PINNED; i++) {
                toml_datum_t d = toml_string_at(pinned, i);
                if (!d.ok) continue;
                strncpy(tmp.dock.pinned[tmp.dock.n_pinned++], d.u.s, DOCK_PINNED_NAME - 1);
                free(d.u.s);
            }
        }
    }
    toml_table_t *widgets = toml_table_in(tbl, "widgets");
    if (widgets) {
        get_bool(widgets, "enabled", &tmp.widgets.enabled);
        get_bool(widgets, "clock", &tmp.widgets.clock);
        get_bool(widgets, "sysmon", &tmp.widgets.sysmon);
        get_int(widgets, "position", &tmp.widgets.position);
        get_int(widgets, "width", &tmp.widgets.width);
        if (tmp.widgets.position < 0) tmp.widgets.position = 0;
        if (tmp.widgets.position > 1) tmp.widgets.position = 1;
        if (tmp.widgets.width < 160) tmp.widgets.width = 160;
        if (tmp.widgets.width > 600) tmp.widgets.width = 600;
    }
    toml_free(tbl);
    memcpy(out, &tmp, sizeof(tmp));
    return true;
}

/* Rewrite config.toml setting <section>.<key> to `value_line`, preserving the
 * rest of the file. The write is atomic (tmp + rename) so the inotify watcher
 * reloads it. `value_line` must include the trailing newline and the full
 * "key = value" text. */
static bool config_write_kv(const char *section, const char *key, const char *value_line)
{
    const char *path = config_user_path();
    if (!path || !path[0] || !section || !key || !value_line) return false;
    ensure_parent_dir(path);

    char *data = NULL;
    size_t len = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
                data = malloc((size_t)sz + 1);
                if (data) {
                    size_t n = fread(data, 1, (size_t)sz, f);
                    data[n] = '\0';
                    len = n;
                }
            }
        }
        fclose(f);
    }
    if (!data) {
        data = malloc(1);
        if (!data) return false;
        data[0] = '\0';
        len = 0;
    }

    const char *line = value_line;
    size_t ln = strlen(line);
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "[%s]", section);
    if (hlen < 0) { free(data); return false; }
    size_t keylen = strlen(key);

    char *out = malloc(len + ln + (size_t)hlen + 16);
    if (!out) { free(data); return false; }
    size_t o = 0;
    bool in_sec = false, saw_sec = false, wrote = false;

    char *p = data;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) + 1 : strlen(p);
        const char *s = p;
        size_t sl = llen;
        while (sl && (*s == ' ' || *s == '\t')) { s++; sl--; }

        if (s[0] == '[') {
            /* leaving the section we never patched: append the key at its end */
            if (in_sec && !wrote) { memcpy(out + o, line, ln); o += ln; wrote = true; }
            in_sec = ((size_t)hlen <= sl && strncmp(s, hdr, (size_t)hlen) == 0 &&
                      (sl == (size_t)hlen || s[hlen] == ' ' || s[hlen] == '\t' ||
                       s[hlen] == '\n' || s[hlen] == '\r' || s[hlen] == '#'));
            saw_sec = saw_sec || in_sec;
        } else if (in_sec && strncmp(s, key, keylen) == 0 &&
                   (s[keylen] == ' ' || s[keylen] == '\t' || s[keylen] == '=')) {
            if (!wrote) {
                memcpy(out + o, line, ln);
                o += ln;
                wrote = true;
            }
            goto next_line; /* replace the first, drop later duplicates */
        }
        memcpy(out + o, p, llen);
        o += llen;
    next_line:
        p += llen;
    }
    if (!saw_sec) {
        out[o++] = '\n';
        memcpy(out + o, hdr, (size_t)hlen); o += (size_t)hlen;
        out[o++] = '\n';
        memcpy(out + o, line, ln); o += ln;
    } else if (!wrote) {
        memcpy(out + o, line, ln); o += ln;
    }
    free(data);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (!f) { free(out); return false; }
    bool ok = fwrite(out, 1, o, f) == o;
    if (fclose(f) != 0) ok = false;
    free(out);
    if (!ok) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

bool config_write_bar_show_weather(bool on)
{
    char line[64];
    snprintf(line, sizeof(line), "show_weather = %s\n", on ? "true" : "false");
    return config_write_kv("bar", "show_weather", line);
}

bool config_write_wallpaper_dir(const char *dir)
{
    if (!dir) return false;
    char line[640];
    snprintf(line, sizeof(line), "dir = \"%s\"\n", dir);
    return config_write_kv("wallpaper", "dir", line);
}

bool config_write_wallpaper_path(const char *path)
{
    if (!path) return false;
    char line[640];
    snprintf(line, sizeof(line), "path = \"%s\"\n", path);
    return config_write_kv("wallpaper", "path", line);
}

bool config_write_theme_dark(bool on)
{
    char line[64];
    snprintf(line, sizeof(line), "dark = %s\n", on ? "true" : "false");
    return config_write_kv("theme", "dark", line);
}

bool config_write_theme_night(bool on)
{
    char line[64];
    snprintf(line, sizeof(line), "night = %s\n", on ? "true" : "false");
    return config_write_kv("theme", "night", line);
}

bool config_write_lock_enabled(bool on)
{
    char line[64];
    snprintf(line, sizeof(line), "enabled = %s\n", on ? "true" : "false");
    return config_write_kv("lock", "enabled", line);
}
