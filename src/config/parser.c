#include "config/parser.h"
#include "config/config.h"
#include "util/log.h"
#include "third_party/tomlc99/toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
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
const char *config_default_path(void)
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
    }
    toml_table_t *font = toml_table_in(tbl, "font");
    if (font) {
        get_string(font, "family", tmp.font.family, sizeof(tmp.font.family));
        get_int(font, "size", &tmp.font.size);
    }
    toml_free(tbl);
    memcpy(out, &tmp, sizeof(tmp));
    return true;
}
