#define _GNU_SOURCE
#include "launcher/launcher.h"
#include "core/wayland.h"
#include "launcher/desktop.h"
#include "launcher/icons.h"
#include "clipboard/clipboard.h"
#include "wallpaper/wallpaper.h"
#include "config/config.h"
#include "util/log.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define ICON_CACHE_SLOTS 64
#define LAUNCHER_MAX_PROVIDERS 8
#define LAUNCHER_MAX_RESULTS 64

struct launcher {
    struct wayland_ctx *ctx;
    bool visible;
    char query[256];
    struct desktop_db db;

    struct launcher_provider *providers[LAUNCHER_MAX_PROVIDERS];
    int n_providers;

    struct launcher_result results[LAUNCHER_MAX_RESULTS];
    int count;

    struct clipboard *clip;
    struct wallpaper *wp;
    const struct config *cfg;

    /* icon cache: result index -> decoded surface (lazily filled) */
    cairo_surface_t *icons[ICON_CACHE_SLOTS];
    int icon_idx[ICON_CACHE_SLOTS];
    int icon_px[ICON_CACHE_SLOTS];
    int icon_n;
};

/* ---- spawning ----------------------------------------------------------- */

static void launcher_spawn(const char *cmd)
{
    if (!cmd) return;
    char buf[512];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    /* strip %U %u %F %f %i %c %k */
    char *p = buf;
    while ((p = strchr(p, '%'))) {
        if (p[1] && strchr("UuFficKk", p[1])) memmove(p, p + 2, strlen(p + 2) + 1);
        else p++;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", buf, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, WNOHANG);
    }
}

/* ---- calculator --------------------------------------------------------- */

struct calc_parser {
    const char *p;
    bool ok;
};

static double calc_expr(struct calc_parser *c);

static void calc_ws(struct calc_parser *c) { while (*c->p == ' ') c->p++; }

static double calc_factor(struct calc_parser *c)
{
    calc_ws(c);
    if (*c->p == '-') { c->p++; return -calc_factor(c); }
    if (*c->p == '+') { c->p++; return calc_factor(c); }
    if (*c->p == '(') {
        c->p++;
        double v = calc_expr(c);
        calc_ws(c);
        if (*c->p == ')') c->p++; else c->ok = false;
        return v;
    }
    char *end = NULL;
    double v = strtod(c->p, &end);
    if (end == c->p) { c->ok = false; return 0; }
    c->p = end;
    return v;
}

static double calc_term(struct calc_parser *c)
{
    double v = calc_factor(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%' && op != 'x' && op != 'X') break;
        if ((op == 'x' || op == 'X') && (isalpha((unsigned char)c->p[1]) && c->p[1] != ' ')) break;
        c->p++;
        double r = calc_factor(c);
        if (op == '*') v *= r;
        else if (op == '/') { if (r == 0) { c->ok = false; return 0; } v /= r; }
        else if (op == '%') { if (r == 0) { c->ok = false; return 0; } v = fmod(v, r); }
        else v *= r; /* x/X as multiply only when isolated */
    }
    return v;
}

static double calc_expr(struct calc_parser *c)
{
    double v = calc_term(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '+' && op != '-') break;
        c->p++;
        double r = calc_term(c);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

/* Evaluate `q` if it looks like an arithmetic expression. Returns true and
 * writes the formatted answer to `out`. */
static bool calc_eval(const char *q, char *out, size_t outsz)
{
    if (!q || !q[0]) return false;
    bool has_digit = false, has_op = false;
    for (const char *p = q; *p; p++) {
        if (isdigit((unsigned char)*p)) has_digit = true;
        else if (strchr("+-*/%()", *p)) has_op = true;
        else if (*p == ' ' || *p == '.' || *p == 'x' || *p == 'X') continue;
        else return false;
    }
    if (!has_digit || !has_op) return false;
    struct calc_parser c = { .p = q, .ok = true };
    double v = calc_expr(&c);
    calc_ws(&c);
    if (!c.ok || *c.p != '\0' || !isfinite(v)) return false;
    if (v == (long long)v) snprintf(out, outsz, "%lld", (long long)v);
    else snprintf(out, outsz, "%.6g", v);
    return true;
}

/* ---- emoji table -------------------------------------------------------- */

struct emoji_entry { const char *glyph; const char *name; };

static const struct emoji_entry emojis[] = {
    { "\xF0\x9F\x98\x80", "smile grin happy cara sonrisa" },
    { "\xF0\x9F\x98\x83", "smile open cara" },
    { "\xF0\x9F\x98\x82", "joy tears risa" },
    { "\xF0\x9F\x98\x85", "sweat sonrisa" },
    { "\xF0\x9F\x98\x86", "sweat grin" },
    { "\xF0\x9F\x98\x89", "wink guino" },
    { "\xF0\x9F\x98\x8A", "blush sonrojo" },
    { "\xF0\x9F\x98\x8D", "heart eyes amor" },
    { "\xF0\x9F\x98\x8E", "sunglasses gafas" },
    { "\xF0\x9F\x98\x98", "kiss beso" },
    { "\xF0\x9F\x98\xAD", "cry llorar" },
    { "\xF0\x9F\x98\xA1", "angry enojado" },
    { "\xF0\x9F\x98\xA2", "sad triste" },
    { "\xF0\x9F\x98\xAE", "neutral" },
    { "\xF0\x9F\x98\xB4", "sleep dormir" },
    { "\xF0\x9F\xA4\x94", "thinking pensar" },
    { "\xF0\x9F\xA4\xA9", "shush silencio" },
    { "\xF0\x9F\x91\x8D", "thumbsup ok pulgar" },
    { "\xF0\x9F\x91\x8E", "thumbsdown pulgar" },
    { "\xF0\x9F\x91\x8F", "clap aplauso" },
    { "\xF0\x9F\x91\x8B", "wave hola saludo" },
    { "\xF0\x9F\x99\x8C", "pray gracias" },
    { "\xF0\x9F\x91\x80", "eyes ojos" },
    { "\xE2\x9D\xA4", "heart corazon amor" },
    { "\xF0\x9F\x94\xA5", "fire fuego" },
    { "\xE2\xAD\x90", "star estrella" },
    { "\xE2\x9C\x85", "check ok" },
    { "\xE2\x9D\x8C", "cross no" },
    { "\xF0\x9F\x93\x8C", "clipboard portapapeles" },
    { "\xF0\x9F\x93\x8E", "pin chincheta" },
    { "\xF0\x9F\x92\xA1", "idea bombilla" },
    { "\xE2\x9A\xA0", "warning alerta" },
    { "\xF0\x9F\x8E\x89", "party fiesta" },
    { "\xF0\x9F\x9A\x80", "rocket cohete" },
    { "\xF0\x9F\x8C\x9F", "globe mundo" },
    { "\xE2\x98\x80", "sun sol" },
    { "\xF0\x9F\x8C\x99", "moon luna" },
    { "\xE2\x9B\x85", "cloud nube" },
    { "\xF0\x9F\x8C\xA7", "rain lluvia" },
    { "\xE2\x9D\x84", "snow nieve" },
};

/* ---- providers ---------------------------------------------------------- */

static int app_match(struct launcher *l, const char *q, struct launcher_result *out, int max)
{
    int idx[256];
    int n = desktop_db_search_fuzzy(&l->db, q, idx, max < 256 ? max : 256);
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        struct launcher_result *r = &out[k++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", l->db.entries[idx[i]].name);
        snprintf(r->desc, sizeof(r->desc), "%s", l->db.entries[idx[i]].generic_name);
        snprintf(r->icon, sizeof(r->icon), "%s", l->db.entries[idx[i]].icon);
        r->type = LR_APP;
        r->app_index = idx[i];
        r->data = NULL;
    }
    return k;
}

static void app_activate(struct launcher *l, struct launcher_result *r)
{
    if (r->app_index >= 0 && r->app_index < l->db.count)
        launcher_spawn(l->db.entries[r->app_index].exec);
}

static int calc_match(struct launcher *l, const char *q, struct launcher_result *out, int max)
{
    (void)l;
    if (max < 1) return 0;
    char ans[64];
    if (!calc_eval(q, ans, sizeof(ans))) return 0;
    struct launcher_result *r = &out[0];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", ans);
    snprintf(r->desc, sizeof(r->desc), "= %s", q);
    snprintf(r->icon, sizeof(r->icon), "%s", "=");
    r->type = LR_CALC;
    r->app_index = -1;
    r->data = strdup(ans);
    return 1;
}

static void calc_activate(struct launcher *l, struct launcher_result *r)
{
    const char *v = r->data ? (const char *)r->data : r->name;
    if (l->clip) { clipboard_add_text(l->clip, v); clipboard_set_text(l->clip, v); }
    log_info("calculator result copied: %s", v);
}

static int emoji_match(struct launcher *l, const char *q, struct launcher_result *out, int max)
{
    (void)l;
    if (!q || strlen(q) < 2) return 0;
    /* allow a leading ':' trigger */
    const char *needle = (q[0] == ':') ? q + 1 : q;
    if (!needle[0]) return 0;
    int k = 0;
    int total = (int)(sizeof(emojis) / sizeof(emojis[0]));
    for (int i = 0; i < total && k < max; i++) {
        if (!strcasestr(emojis[i].name, needle)) continue;
        struct launcher_result *r = &out[k++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", emojis[i].glyph);
        snprintf(r->desc, sizeof(r->desc), "%s", emojis[i].name);
        snprintf(r->icon, sizeof(r->icon), "%s", emojis[i].glyph);
        r->type = LR_EMOJI;
        r->app_index = -1;
        r->data = strdup(emojis[i].glyph);
    }
    return k;
}

static void emoji_activate(struct launcher *l, struct launcher_result *r)
{
    const char *g = r->data ? (const char *)r->data : r->name;
    if (l->clip) { clipboard_add_text(l->clip, g); clipboard_set_text(l->clip, g); }
    log_info("emoji copied: %s", g);
}

static bool is_image_name(const char *n)
{
    const char *dot = strrchr(n, '.');
    if (!dot) return false;
    static const char *exts[] = { ".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", NULL };
    for (int i = 0; exts[i]; i++)
        if (strcasecmp(dot, exts[i]) == 0) return true;
    return false;
}

static int wallpaper_scan_dir(const char *dir, const char *q, struct launcher_result *out, int max, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int k = 0;
    char subdirs[16][1024];
    int n_sub = 0;
    while ((e = readdir(d)) && k < max) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (!is_image_name(e->d_name)) {
            if (depth > 0 && n_sub < 16) {
                struct stat st;
                if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
                    snprintf(subdirs[n_sub++], sizeof(subdirs[0]), "%s", full);
            }
            continue;
        }
        if (!strcasestr(e->d_name, q)) continue;
        struct launcher_result *r = &out[k++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", e->d_name);
        snprintf(r->desc, sizeof(r->desc), "%s", dir);
        snprintf(r->icon, sizeof(r->icon), "%s", "\xF0\x9F\x96\xBC"); /* 🖼 */
        r->type = LR_WALLPAPER;
        r->app_index = -1;
        r->data = strdup(full);
    }
    closedir(d);
    for (int i = 0; i < n_sub && k < max; i++)
        k += wallpaper_scan_dir(subdirs[i], q, &out[k], max - k, depth - 1);
    return k;
}

static int wallpaper_match(struct launcher *l, const char *q, struct launcher_result *out, int max)
{
    if (!l->wp || !q || strlen(q) < 2) return 0;
    const char *dirs[3];
    int nd = 0;
    const char *own = wallpaper_get_dir(l->wp);
    if (own && own[0]) dirs[nd++] = own;
    dirs[nd++] = "/usr/share/wallpapers";
    dirs[nd++] = "/usr/share/backgrounds";
    int k = 0;
    for (int i = 0; i < nd && k < max; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) if (strcmp(dirs[j], dirs[i]) == 0) dup = true;
        if (dup) continue;
        k += wallpaper_scan_dir(dirs[i], q, &out[k], max - k, 1);
    }
    return k;
}

static void wallpaper_activate(struct launcher *l, struct launcher_result *r)
{
    if (l->wp && r->data) {
        wallpaper_set_path(l->wp, (const char *)r->data);
        log_info("wallpaper set from launcher: %s", (const char *)r->data);
    }
}

static int run_match(struct launcher *l, const char *q, struct launcher_result *out, int max)
{
    (void)l;
    if (!q || q[0] != '>' || max < 1) return 0;
    const char *cmd = q + 1;
    while (*cmd == ' ') cmd++;
    if (!cmd[0]) return 0;
    struct launcher_result *r = &out[0];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", cmd);
    snprintf(r->desc, sizeof(r->desc), "%s", "Ejecutar comando");
    snprintf(r->icon, sizeof(r->icon), "%s", ">");
    r->type = LR_GENERIC;
    r->app_index = -1;
    r->data = strdup(cmd);
    return 1;
}

static void run_activate(struct launcher *l, struct launcher_result *r)
{
    (void)l;
    if (r->data) launcher_spawn((const char *)r->data);
}

static struct launcher_provider prov_app = { .name = "apps", .match = app_match, .activate = app_activate };
static struct launcher_provider prov_calc = { .name = "calc", .match = calc_match, .activate = calc_activate };
static struct launcher_provider prov_emoji = { .name = "emoji", .match = emoji_match, .activate = emoji_activate };
static struct launcher_provider prov_wp = { .name = "wallpapers", .match = wallpaper_match, .activate = wallpaper_activate };
static struct launcher_provider prov_run = { .name = "run", .match = run_match, .activate = run_activate };

void launcher_register_default_providers(struct launcher *l)
{
    /* order matters: calc first, then apps, then the optional sources */
    launcher_register_provider(l, &prov_calc);
    launcher_register_provider(l, &prov_app);
    launcher_register_provider(l, &prov_emoji);
    launcher_register_provider(l, &prov_wp);
    launcher_register_provider(l, &prov_run);
}

/* ---- icon cache --------------------------------------------------------- */

cairo_surface_t *launcher_icon_for(struct launcher *l, int idx, int px)
{
    if (!l || idx < 0 || idx >= l->count) return NULL;
    if (l->results[idx].type != LR_APP) return NULL;
    int ai = l->results[idx].app_index;
    if (ai < 0 || ai >= l->db.count) return NULL;
    if (px <= 0) px = 22;
    for (int i = 0; i < l->icon_n; i++)
        if (l->icon_idx[i] == ai && l->icon_px[i] == px) return l->icons[i];
    if (l->icon_n >= ICON_CACHE_SLOTS) return NULL;
    const char *name = l->db.entries[ai].icon;
    cairo_surface_t *s = name[0] ? icon_load_surface(name, px) : NULL;
    l->icon_idx[l->icon_n] = ai;
    l->icon_px[l->icon_n] = px;
    l->icons[l->icon_n] = s;
    l->icon_n++;
    return s;
}

/* ---- lifecycle ---------------------------------------------------------- */

void launcher_register_provider(struct launcher *l, struct launcher_provider *p)
{
    if (!l || !p || l->n_providers >= LAUNCHER_MAX_PROVIDERS) return;
    l->providers[l->n_providers++] = p;
}

struct launcher *launcher_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct launcher *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->ctx = ctx;
    l->visible = false;
    l->query[0] = '\0';
    desktop_db_init(&l->db);
    desktop_db_load_system(&l->db);
    launcher_register_default_providers(l);
    log_info("launcher loaded %d desktop entries (%d providers)", l->db.count, l->n_providers);
    return l;
}

void launcher_destroy(struct launcher *l)
{
    if (!l) return;
    for (int i = 0; i < l->count; i++)
        if (l->results[i].data) free(l->results[i].data);
    for (int i = 0; i < l->icon_n; i++)
        if (l->icons[i]) cairo_surface_destroy(l->icons[i]);
    desktop_db_free(&l->db);
    free(l);
}

void launcher_set_clipboard(struct launcher *l, struct clipboard *clip) { if (l) l->clip = clip; }
void launcher_set_wallpaper(struct launcher *l, struct wallpaper *wp) { if (l) l->wp = wp; }
void launcher_set_config(struct launcher *l, const struct config *cfg) { if (l) l->cfg = cfg; }

void launcher_show(struct launcher *l)
{
    if (!l) return;
    l->visible = true;
    l->query[0] = '\0';
    launcher_set_query(l, "");
}

void launcher_hide(struct launcher *l)
{
    if (!l) return;
    l->visible = false;
}

void launcher_toggle(struct launcher *l)
{
    if (!l) return;
    if (l->visible) launcher_hide(l);
    else launcher_show(l);
}

bool launcher_is_visible(struct launcher *l)
{
    return l && l->visible;
}

void launcher_set_query(struct launcher *l, const char *q)
{
    if (!l || !q) return;
    strncpy(l->query, q, sizeof(l->query) - 1);
    l->query[sizeof(l->query) - 1] = '\0';

    for (int i = 0; i < l->count; i++)
        if (l->results[i].data) { free(l->results[i].data); l->results[i].data = NULL; }
    l->count = 0;

    /* empty query: browse all apps */
    bool empty = l->query[0] == '\0';
    int cap = LAUNCHER_MAX_RESULTS;
    for (int i = 0; i < l->n_providers && l->count < cap; i++) {
        struct launcher_provider *p = l->providers[i];
        if (!p || !p->match) continue;
        if (empty && p != &prov_app) continue;
        int n = p->match(l, l->query, &l->results[l->count], cap - l->count);
        if (n < 0) n = 0;
        l->count += n;
    }
    l->icon_n = 0; /* result set changed: drop cached surfaces */
}

void launcher_get_query(struct launcher *l, char *out, size_t outsz)
{
    if (!l || !out || outsz == 0) return;
    strncpy(out, l->query, outsz - 1);
    out[outsz - 1] = '\0';
}

void launcher_get_results_list(struct launcher *l, struct launcher_result *out, int *count_out, int maxn)
{
    if (!l || !out || !count_out || maxn <= 0) return;
    int n = l->count < maxn ? l->count : maxn;
    memcpy(out, l->results, n * sizeof(struct launcher_result));
    *count_out = n;
}

void launcher_activate_result(struct launcher *l, struct launcher_result *r)
{
    if (!l || !r) return;
    for (int i = 0; i < l->n_providers; i++) {
        struct launcher_provider *p = l->providers[i];
        if (!p || !p->activate) continue;
        if ((r->type == LR_APP && p == &prov_app) ||
            (r->type == LR_CALC && p == &prov_calc) ||
            (r->type == LR_EMOJI && p == &prov_emoji) ||
            (r->type == LR_WALLPAPER && p == &prov_wp) ||
            (r->type == LR_GENERIC && p == &prov_run)) {
            p->activate(l, r);
            l->visible = false;
            return;
        }
    }
}

void launcher_exec_selected(struct launcher *l)
{
    if (!l || l->count <= 0) return;
    launcher_activate_result(l, &l->results[0]);
}
