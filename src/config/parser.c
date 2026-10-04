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
    }
    toml_table_t *theme = toml_table_in(tbl, "theme");
    if (theme) {
        toml_table_t *bg = toml_table_in(theme, "background");
        if (bg) {
            double v;
            if (get_double(bg, "r", &v)) tmp.theme.background.r = (float)v;
            if (get_double(bg, "g", &v)) tmp.theme.background.g = (float)v;
            if (get_double(bg, "b", &v)) tmp.theme.background.b = (float)v;
            if (get_double(bg, "a", &v)) tmp.theme.background.a = (float)v;
        }
        toml_table_t *fg = toml_table_in(theme, "foreground");
        if (fg) {
            double v;
            if (get_double(fg, "r", &v)) tmp.theme.foreground.r = (float)v;
            if (get_double(fg, "g", &v)) tmp.theme.foreground.g = (float)v;
            if (get_double(fg, "b", &v)) tmp.theme.foreground.b = (float)v;
            if (get_double(fg, "a", &v)) tmp.theme.foreground.a = (float)v;
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
