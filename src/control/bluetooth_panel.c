#include "control/bluetooth_panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "util/log.h"
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <strings.h>
#include <time.h>

#define BT_W 360
#define BT_PAD 18.0
#define BT_HEAD_H 40.0
#define BT_STATUS_H 34.0
#define BT_SEC_GAP 22.0
#define BT_ROW_H 44.0
#define BT_VISIBLE 6
#define BT_ACT_H 34.0
#define BT_MIN_H 460

#define BT_MAX_DEV 24
#define BT_MAX_INFO 16   /* devices we query with `info` */
#define BT_RESULT "/tmp/emancipation-bt.result"

enum {
    BT_HOVER_NONE = -1,
    BT_HOVER_POWER = 1,
    BT_HOVER_SCAN = 2,
    BT_HOVER_PRIMARY = 3,
    BT_HOVER_FORGET = 4,
    BT_HOVER_DEV_BASE = 100,
};

struct bt_dev {
    char mac[24];
    char name[64];
    bool paired;
    bool connected;
    bool trusted;
    int battery; /* 0..100, -1 unknown */
};

struct bluetooth_panel {
    struct panel panel;
    const struct config *cfg;
    struct output *fallback_out;
    PangoLayout *l_title;
    PangoLayout *l_label;
    PangoLayout *l_value;

    char btctl[512];
    bool has_btctl;
    char adapter[64];
    bool has_adapter;
    bool powered;
    bool discovering;

    struct bt_dev devs[BT_MAX_DEV];
    int n_devs;
    int sel;
    int scroll;

    int hover;
    double pwr_x, pwr_w;   /* powered toggle button (right of the title) */
    double scan_x, scan_w; /* scan button */
    double dev_y0;         /* first device row top */
    double act_y;          /* action button row top, 0 when hidden */
    double act_x1, act_w1; /* primary action button */
    double act_x2, act_w2; /* forget button */

    bool scanning;
    double scan_until;     /* monotonic seconds */
    bool pending;          /* an action is running */
    double next_refresh;   /* monotonic seconds */
    char pending_ok[96];
    char pending_err[96];
    char msg[200];
    int msg_kind;          /* 0 none, 1 info, 2 error, 3 ok */
};

/* ------------------------------------------------------------------ helpers */

static double bt_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool bt_tool_path(const char *name, char *out, size_t n)
{
    const char *dirs[] = { "/usr/bin", "/usr/local/bin", "/bin", "/sbin" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(out, n, "%s/%s", dirs[i], name);
        if (access(out, X_OK) == 0) return true;
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(out, n, "%s/.local/bin/%s", home, name);
        if (access(out, X_OK) == 0) return true;
    }
    if (out && n) out[0] = '\0';
    return false;
}

static bool bt_run(const char *cmd, char *out, size_t n)
{
    if (out && n) out[0] = '\0';
    FILE *f = popen(cmd, "r");
    if (!f) return false;
    size_t r = out ? fread(out, 1, n - 1, f) : 0;
    if (out) out[r] = '\0';
    int rc = pclose(f);
    return rc == 0;
}

static void bt_shell_quote(const char *in, char *out, size_t n)
{
    if (!out || n == 0) return;
    size_t o = 0;
    if (o + 1 < n) out[o++] = '\'';
    for (const char *p = in; *p && o + 5 < n; p++) {
        if (*p == '\'') {
            out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\'';
        } else {
            out[o++] = *p;
        }
    }
    if (o + 1 < n) out[o++] = '\'';
    out[o] = '\0';
}

/* Bluetooth addresses are well-formed MACs; reject anything else before it is
 * interpolated into a command line. */
static bool bt_mac_ok(const char *mac)
{
    if (!mac || strlen(mac) != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = mac[i];
        if ((i % 3) == 2) { if (c != ':') return false; }
        else if (!isxdigit((unsigned char)c)) return false;
    }
    return true;
}

static void bt_spawn(const char *cmd)
{
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int dn = open("/dev/null", O_RDONLY);
    if (dn >= 0) { dup2(dn, 0); if (dn > 2) close(dn); }
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    _exit(127);
}

/* Build "<bluetoothctl> [extra] <args...>" with proper quoting. */
static void bt_cmd(struct bluetooth_panel *bp, char *out, size_t n,
                   const char *extra, const char *args)
{
    char q[600];
    bt_shell_quote(bp->btctl, q, sizeof(q));
    if (extra && extra[0]) snprintf(out, n, "%s %s %s", q, extra, args);
    else snprintf(out, n, "%s %s", q, args);
}

/* ------------------------------------------------------------------ gathering */

/* `bluetoothctl show` prints "Powered: yes" / "Discovering: no" / "Name: X". */
static void bt_parse_bools(const char *buf, struct bluetooth_panel *bp)
{
    const char *p = strstr(buf, "Powered:");
    if (p) {
        const char *v = strchr(p + 8, ' ');
        bp->powered = v && v[1] == 'y' && v[2] == 'e' && v[3] == 's';
    }
    p = strstr(buf, "Discovering:");
    if (p) {
        const char *v = strchr(p + 12, ' ');
        bp->discovering = v && v[1] == 'y' && v[2] == 'e' && v[3] == 's';
    }
    p = strstr(buf, "Name:");
    if (p) {
        const char *s = p + 5;
        while (*s == ' ') s++;
        size_t i = 0;
        while (*s && *s != '\n' && i + 1 < sizeof(bp->adapter)) bp->adapter[i++] = *s++;
        while (i && isspace((unsigned char)bp->adapter[i - 1])) i--;
        bp->adapter[i] = '\0';
    }
}

static struct bt_dev *bt_find(struct bluetooth_panel *bp, const char *mac)
{
    for (int i = 0; i < bp->n_devs; i++)
        if (strcmp(bp->devs[i].mac, mac) == 0) return &bp->devs[i];
    return NULL;
}

static void bt_dev_add(struct bluetooth_panel *bp, const char *mac, const char *name)
{
    if (!bt_mac_ok(mac) || bp->n_devs >= BT_MAX_DEV) return;
    if (bt_find(bp, mac)) return;
    struct bt_dev *d = &bp->devs[bp->n_devs++];
    snprintf(d->mac, sizeof(d->mac), "%s", mac);
    if (name && name[0]) snprintf(d->name, sizeof(d->name), "%s", name);
    else snprintf(d->name, sizeof(d->name), "%s", mac);
    d->paired = false;
    d->connected = false;
    d->trusted = false;
    d->battery = -1;
}

static void bt_parse_device_line(const char *line, bool paired, struct bluetooth_panel *bp)
{
    /* "Device AA:BB:CC:DD:EE:FF Some Name" */
    const char *p = line;
    if (strncmp(p, "Device ", 7) != 0) return;
    p += 7;
    char mac[24];
    int i = 0;
    while (*p && *p != ' ' && i < (int)sizeof(mac) - 1) mac[i++] = *p++;
    mac[i] = '\0';
    if (*p == ' ') p++;
    char name[64];
    int j = 0;
    while (*p && *p != '\n' && j < (int)sizeof(name) - 1) name[j++] = *p++;
    while (j && isspace((unsigned char)name[j - 1])) j--;
    name[j] = '\0';
    struct bt_dev *d = bt_find(bp, mac);
    if (!d) {
        bt_dev_add(bp, mac, name);
        d = bt_find(bp, mac);
    }
    if (d && paired) d->paired = true;
}

/* Parse one `bluetoothctl info <mac>` block. */
static void bt_parse_info(struct bt_dev *d, const char *buf)
{
    const char *p = strstr(buf, "Paired:");
    if (p) {
        const char *v = strchr(p + 7, ' ');
        d->paired = v && v[1] == 'y' && v[2] == 'e' && v[3] == 's';
    }
    p = strstr(buf, "Connected:");
    if (p) {
        const char *v = strchr(p + 10, ' ');
        d->connected = v && v[1] == 'y' && v[2] == 'e' && v[3] == 's';
    }
    p = strstr(buf, "Trusted:");
    if (p) {
        const char *v = strchr(p + 8, ' ');
        d->trusted = v && v[1] == 'y' && v[2] == 'e' && v[3] == 's';
    }
    p = strstr(buf, "Battery Percentage");
    if (p) {
        const char *colon = strchr(p, ':');
        if (colon) {
            const char *s = colon + 1;
            while (*s && !isdigit((unsigned char)*s)) s++;
            if (isdigit((unsigned char)*s)) {
                int pct = atoi(s);
                if (pct >= 0 && pct <= 100) d->battery = pct;
            }
        }
    }
    p = strstr(buf, "Name:");
    if (p) {
        const char *s = p + 5;
        while (*s == ' ') s++;
        int k = 0;
        char nm[64];
        while (*s && *s != '\n' && k < (int)sizeof(nm) - 1) nm[k++] = *s++;
        while (k && isspace((unsigned char)nm[k - 1])) k--;
        nm[k] = '\0';
        if (k) snprintf(d->name, sizeof(d->name), "%s", nm);
    }
}

static int bt_cmp_dev(const void *a, const void *b)
{
    const struct bt_dev *x = a, *y = b;
    if (x->connected != y->connected) return x->connected ? -1 : 1;
    if (x->paired != y->paired) return x->paired ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void bt_gather(struct bluetooth_panel *bp)
{
    bp->has_btctl = bt_tool_path("bluetoothctl", bp->btctl, sizeof(bp->btctl));
    if (!bp->has_btctl) {
        bp->has_adapter = false;
        bp->n_devs = 0;
        return;
    }

    char cmd[900], buf[4096];
    bt_cmd(bp, cmd, sizeof(cmd), "", "show");
    bp->has_adapter = bt_run(cmd, buf, sizeof(buf));
    if (!bp->has_adapter) {
        bp->n_devs = 0;
        return;
    }
    bt_parse_bools(buf, bp);
    if (!bp->adapter[0]) snprintf(bp->adapter, sizeof(bp->adapter), "Adaptador Bluetooth");

    bp->n_devs = 0;
    bt_cmd(bp, cmd, sizeof(cmd), "", "devices");
    if (bt_run(cmd, buf, sizeof(buf))) {
        char *save = buf;
        while (*save) {
            char *nl = strchr(save, '\n');
            if (nl) *nl = '\0';
            if (strncmp(save, "Device ", 7) == 0) bt_parse_device_line(save, false, bp);
            if (!nl) break;
            save = nl + 1;
        }
    }
    bt_cmd(bp, cmd, sizeof(cmd), "", "paired-devices");
    if (bt_run(cmd, buf, sizeof(buf))) {
        char *save = buf;
        while (*save) {
            char *nl = strchr(save, '\n');
            if (nl) *nl = '\0';
            if (strncmp(save, "Device ", 7) == 0) bt_parse_device_line(save, true, bp);
            if (!nl) break;
            save = nl + 1;
        }
    }

    int queried = bp->n_devs < BT_MAX_INFO ? bp->n_devs : BT_MAX_INFO;
    for (int i = 0; i < queried; i++) {
        char qmac[40], qcmd[1000];
        bt_shell_quote(bp->devs[i].mac, qmac, sizeof(qmac));
        bt_cmd(bp, qcmd, sizeof(qcmd), "", "info");
        size_t used = strlen(qcmd);
        snprintf(qcmd + used, sizeof(qcmd) - used, " %s", qmac);
        if (bt_run(qcmd, buf, sizeof(buf))) bt_parse_info(&bp->devs[i], buf);
    }

    qsort(bp->devs, (size_t)bp->n_devs, sizeof(bp->devs[0]), bt_cmp_dev);
    if (bp->sel >= bp->n_devs) bp->sel = bp->n_devs > 0 ? bp->n_devs - 1 : 0;
    if (bp->sel < 0) bp->sel = 0;
}

/* ------------------------------------------------------------------ actions */

/* Run one bluetoothctl action, capturing its exit status so the panel can
 * report success or failure (mirrors the Wi-Fi panel). */
static void bt_action(struct bluetooth_panel *bp, const char *args, double delay,
                      const char *busy, const char *done, const char *err_msg)
{
    if (!bp->has_btctl) return;
    char cmd[1100];
    bt_cmd(bp, cmd, sizeof(cmd), "", args);
    size_t used = strlen(cmd);
    snprintf(cmd + used, sizeof(cmd) - used,
             " > " BT_RESULT " 2>&1; echo \"RC=$?\" >> " BT_RESULT);
    unlink(BT_RESULT);
    bt_spawn(cmd);
    bp->pending = true;
    bp->next_refresh = bt_now() + delay;
    snprintf(bp->pending_ok, sizeof(bp->pending_ok), "%s", done ? done : "Hecho");
    snprintf(bp->pending_err, sizeof(bp->pending_err), "%s", err_msg ? err_msg : "Error");
    if (busy) {
        snprintf(bp->msg, sizeof(bp->msg), "%s", busy);
        bp->msg_kind = 1;
    }
}

static void bt_schedule_scan(struct bluetooth_panel *bp, bool on)
{
    if (!bp->has_btctl) return;
    char cmd[1100];
    bt_cmd(bp, cmd, sizeof(cmd), "", on ? "scan on" : "scan off");
    size_t used = strlen(cmd);
    snprintf(cmd + used, sizeof(cmd) - used,
             " > /dev/null 2>&1");
    bt_spawn(cmd);
}

static void bt_read_result(struct bluetooth_panel *bp)
{
    FILE *f = fopen(BT_RESULT, "r");
    if (!f) return;
    char buf[1024];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = '\0';
    unlink(BT_RESULT);

    int rc = -1;
    char *rcp = strstr(buf, "RC=");
    if (rcp) rc = atoi(rcp + 3);
    if (rc == 0) {
        snprintf(bp->msg, sizeof(bp->msg), "%s", bp->pending_ok);
        bp->msg_kind = 3;
        return;
    }
    char err[100] = "";
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (*p && strncmp(p, "RC=", 3) != 0) snprintf(err, sizeof(err), "%.80s", p);
        if (!nl) break;
        p = nl + 1;
    }
    if (err[0]) snprintf(bp->msg, sizeof(bp->msg), "%s: %s", bp->pending_err, err);
    else snprintf(bp->msg, sizeof(bp->msg), "%s", bp->pending_err);
    bp->msg_kind = 2;
}

static int bt_do(struct bluetooth_panel *bp, int idx, const char *what, double delay,
                 const char *msg, const char *done, const char *err)
{
    if (idx < 0 || idx >= bp->n_devs || !bt_mac_ok(bp->devs[idx].mac)) {
        if (err) {
            snprintf(bp->msg, sizeof(bp->msg), "%s", err);
            bp->msg_kind = 2;
        }
        return -1;
    }
    char qmac[40], args[80];
    bt_shell_quote(bp->devs[idx].mac, qmac, sizeof(qmac));
    snprintf(args, sizeof(args), "%s %s", what, qmac);
    bt_action(bp, args, delay, msg, done, err);
    return 0;
}

static int bt_primary(struct bluetooth_panel *bp, int idx)
{
    if (idx < 0 || idx >= bp->n_devs) return -1;
    struct bt_dev *d = &bp->devs[idx];
    char m[96], done[96];
    if (d->connected) {
        snprintf(m, sizeof(m), "Desconectando %s\xE2\x80\xA6", d->name);
        snprintf(done, sizeof(done), "Desconectado: %s", d->name);
        return bt_do(bp, idx, "disconnect", 1.5, m, done, "no se pudo desconectar");
    }
    if (d->paired) {
        snprintf(m, sizeof(m), "Conectando %s\xE2\x80\xA6", d->name);
        snprintf(done, sizeof(done), "Conectado: %s", d->name);
        return bt_do(bp, idx, "connect", 3.0, m, done, "no se pudo conectar");
    }
    snprintf(m, sizeof(m), "Emparejando %s\xE2\x80\xA6", d->name);
    snprintf(done, sizeof(done), "Emparejado: %s", d->name);
    if (!bp->has_btctl) return -1;
    /* register an unattended agent so `pair` can complete without a console */
    char cmd[1000];
    bt_cmd(bp, cmd, sizeof(cmd), "--agent NoInputNoOutput", "default-agent");
    bt_spawn(cmd);
    return bt_do(bp, idx, "pair", 8.0, m, done, "no se pudo emparejar");
}

/* ------------------------------------------------------------------ drawing */

static void bt_round(cairo_t *cr, double x, double y, double w, double h, double r)
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

static const char *bt_primary_label(const struct bt_dev *d)
{
    if (d->connected) return "Desconectar";
    if (d->paired) return "Conectar";
    return "Emparejar";
}

static void bt_button(cairo_t *cr, const struct theme_config *th, PangoLayout *lay,
                      double x, double y, double w, double h, const char *text, bool hot)
{
    bt_round(cr, x, y, w, h, 9);
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                          th->surface_variant.b, hot ? 1.0 : 0.9);
    cairo_fill(cr);
    pango_layout_set_text(lay, text, -1);
    int tw = 0, thh = 0;
    pango_layout_get_pixel_size(lay, &tw, &thh);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
    cairo_move_to(cr, x + (w - tw) / 2, y + (h - thh) / 2);
    pango_cairo_show_layout(cr, lay);
}

static void bt_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct bluetooth_panel *bp = p->userdata;
    (void)h;
    struct config def;
    config_defaults(&def);
    const struct theme_config *th = bp->cfg ? &bp->cfg->theme : &def.theme;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!bp->l_title) {
        bp->l_title = pango_cairo_create_layout(cr);
        bp->l_label = pango_cairo_create_layout(cr);
        bp->l_value = pango_cairo_create_layout(cr);
    }
    const char *family = (bp->cfg && bp->cfg->font.family[0]) ? bp->cfg->font.family : "sans-serif";
    double fsize = (bp->cfg && bp->cfg->font.size > 0) ? bp->cfg->font.size : 13.0;
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)((fsize + 3) * PANGO_SCALE));
    pango_font_description_set_weight(fd, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(bp->l_title, fd);
    pango_font_description_set_size(fd, (int)((fsize - 1) * PANGO_SCALE));
    pango_layout_set_font_description(bp->l_label, fd);
    pango_font_description_set_weight(fd, PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(bp->l_value, fd);
    pango_font_description_free(fd);

    double y = BT_PAD;

    /* header: title + powered toggle */
    pango_layout_set_text(bp->l_title, "Bluetooth", -1);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_move_to(cr, BT_PAD, y);
    pango_cairo_show_layout(cr, bp->l_title);

    const char *pwr_txt = bp->powered ? "Activado" : "Desactivado";
    int pw = 0;
    pango_layout_set_text(bp->l_label, pwr_txt, -1);
    pango_layout_get_pixel_size(bp->l_label, &pw, NULL);
    bp->pwr_w = pw + 24;
    bp->pwr_x = w - BT_PAD - bp->pwr_w;
    bt_button(cr, th, bp->l_label, bp->pwr_x, y + 2, bp->pwr_w, 24.0, pwr_txt,
              bp->hover == BT_HOVER_POWER);
    y += BT_HEAD_H;

    /* status row */
    const char *st = !bp->has_btctl ? "bluetoothctl no disponible"
                    : !bp->has_adapter ? "Sin adaptador Bluetooth"
                    : !bp->powered ? "Bluetooth desactivado"
                    : bp->discovering ? "Escaneando\xE2\x80\xA6"
                    : "Listo";
    pango_layout_set_text(bp->l_value, st, -1);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.9);
    cairo_move_to(cr, BT_PAD, y + 6);
    pango_cairo_show_layout(cr, bp->l_value);

    const char *scan_txt = bp->scanning ? "Parar" : "Escanear";
    int sw = 0;
    pango_layout_set_text(bp->l_label, scan_txt, -1);
    pango_layout_get_pixel_size(bp->l_label, &sw, NULL);
    bp->scan_w = sw + 24;
    bt_button(cr, th, bp->l_label, w - BT_PAD - bp->scan_w, y, bp->scan_w, 26.0, scan_txt,
              bp->hover == BT_HOVER_SCAN);
    bp->scan_x = w - BT_PAD - bp->scan_w;
    y += BT_STATUS_H + 6;

    /* device list */
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.85);
    pango_layout_set_text(bp->l_label, "Dispositivos", -1);
    cairo_move_to(cr, BT_PAD, y);
    pango_cairo_show_layout(cr, bp->l_label);
    y += BT_SEC_GAP;
    bp->dev_y0 = y;

    int vis = bp->n_devs < BT_VISIBLE ? bp->n_devs : BT_VISIBLE;
    pango_layout_set_width(bp->l_label, (int)((w - 2 * BT_PAD - 96) * PANGO_SCALE));
    pango_layout_set_ellipsize(bp->l_label, PANGO_ELLIPSIZE_END);
    for (int i = 0; i < vis; i++) {
        int idx = bp->scroll + i;
        if (idx >= bp->n_devs) break;
        struct bt_dev *d = &bp->devs[idx];
        double ry = y + i * BT_ROW_H;
        bool sel = (idx == bp->sel);
        bool hov = (bp->hover == BT_HOVER_DEV_BASE + i);
        bt_round(cr, BT_PAD, ry, w - 2 * BT_PAD, BT_ROW_H - 8, 10);
        if (d->connected)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.30);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, (sel || hov) ? 1.0 : 0.75);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.98);
        pango_layout_set_text(bp->l_label, d->name, -1);
        cairo_move_to(cr, BT_PAD + 12, ry + 5);
        pango_cairo_show_layout(cr, bp->l_label);

        const char *sub = d->connected ? "Conectado"
                        : d->paired ? "Pareado"
                        : "Disponible";
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.9);
        pango_layout_set_text(bp->l_value, sub, -1);
        cairo_move_to(cr, BT_PAD + 12, ry + 22);
        pango_cairo_show_layout(cr, bp->l_value);

        char tag[24];
        if (d->battery >= 0) snprintf(tag, sizeof(tag), "%d%%", d->battery);
        else snprintf(tag, sizeof(tag), "%s", d->paired ? "\xE2\x80\xA2" : "");
        pango_layout_set_text(bp->l_value, tag, -1);
        int tw = 0, thh = 0;
        pango_layout_get_pixel_size(bp->l_value, &tw, &thh);
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.9);
        cairo_move_to(cr, w - BT_PAD - 12 - tw, ry + 12);
        pango_cairo_show_layout(cr, bp->l_value);
    }
    pango_layout_set_width(bp->l_label, -1);
    pango_layout_set_ellipsize(bp->l_label, PANGO_ELLIPSIZE_NONE);
    y += BT_VISIBLE * BT_ROW_H;

    /* actions for the selected device */
    bp->act_y = 0;
    if (bp->sel >= 0 && bp->sel < bp->n_devs) {
        struct bt_dev *d = &bp->devs[bp->sel];
        double bw = w - 2 * BT_PAD;
        double gap = 8.0;
        bp->act_y = y + 4;
        if (d->paired) {
            bp->act_w1 = (bw - gap) / 2;
            bp->act_x1 = BT_PAD;
            bt_button(cr, th, bp->l_label, bp->act_x1, bp->act_y, bp->act_w1, BT_ACT_H,
                      bt_primary_label(d), bp->hover == BT_HOVER_PRIMARY);
            bp->act_w2 = bp->act_w1;
            bp->act_x2 = BT_PAD + bp->act_w1 + gap;
            bt_button(cr, th, bp->l_label, bp->act_x2, bp->act_y, bp->act_w2, BT_ACT_H,
                      "Olvidar", bp->hover == BT_HOVER_FORGET);
            y = bp->act_y + BT_ACT_H;
        } else {
            bp->act_w1 = bw;
            bp->act_x1 = BT_PAD;
            bt_button(cr, th, bp->l_label, bp->act_x1, bp->act_y, bp->act_w1, BT_ACT_H,
                      bt_primary_label(d), bp->hover == BT_HOVER_PRIMARY);
            bp->act_w2 = 0;
            bp->act_x2 = 0;
            y = bp->act_y + BT_ACT_H;
        }
    }

    if (bp->msg[0]) {
        cairo_set_source_rgba(cr,
                              bp->msg_kind == 2 ? th->error.r : th->on_surface_variant.r,
                              bp->msg_kind == 2 ? th->error.g : th->on_surface_variant.g,
                              bp->msg_kind == 2 ? th->error.b : th->on_surface_variant.b, 0.95);
        pango_layout_set_text(bp->l_value, bp->msg, -1);
        pango_layout_set_width(bp->l_value, (int)((w - 2 * BT_PAD) * PANGO_SCALE));
        pango_layout_set_ellipsize(bp->l_value, PANGO_ELLIPSIZE_END);
        cairo_move_to(cr, BT_PAD, y + 10);
        pango_cairo_show_layout(cr, bp->l_value);
        pango_layout_set_width(bp->l_value, -1);
        pango_layout_set_ellipsize(bp->l_value, PANGO_ELLIPSIZE_NONE);
    }
}

/* ------------------------------------------------------------------- input */

static void bt_sel_ensure_visible(struct bluetooth_panel *bp)
{
    if (bp->sel < bp->scroll) bp->scroll = bp->sel;
    if (bp->sel >= bp->scroll + BT_VISIBLE)
        bp->scroll = bp->sel - BT_VISIBLE + 1;
    if (bp->scroll < 0) bp->scroll = 0;
}

static bool bt_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)mods;
    struct bluetooth_panel *bp = p->userdata;
    if (keysym == 0xff1b) { /* Escape */
        panel_hide(p);
        return true;
    }
    if (keysym == 0xff52 && bp->n_devs) { /* Up */
        if (bp->sel > 0) bp->sel--;
        bt_sel_ensure_visible(bp);
        panel_mark_dirty(p);
        return true;
    }
    if (keysym == 0xff54 && bp->n_devs) { /* Down */
        if (bp->sel + 1 < bp->n_devs) bp->sel++;
        bt_sel_ensure_visible(bp);
        panel_mark_dirty(p);
        return true;
    }
    if (keysym == 0xff0d || keysym == 0xff8d) { /* Return */
        if (bt_primary(bp, bp->sel) == 0) panel_mark_dirty(p);
        return true;
    }
    if (keysym == 0x72 || keysym == 0x52) { /* r / R: rescan */
        if (!bp->scanning && bp->powered) {
            bt_schedule_scan(bp, true);
            bp->scanning = true;
            bp->scan_until = bt_now() + 15.0;
            bp->next_refresh = bt_now() + 2.0;
            panel_mark_dirty(p);
        }
        return true;
    }
    return true;
}

static bool bt_click(struct panel *p, double x, double y)
{
    struct bluetooth_panel *bp = p->userdata;

    if (y >= BT_PAD && y <= BT_PAD + 28.0 && x >= bp->pwr_x) {
        if (bp->has_btctl)
            bt_action(bp, bp->powered ? "power off" : "power on", 1.5,
                      bp->powered ? "Desactivando\xE2\x80\xA6" : "Activando\xE2\x80\xA6",
                      bp->powered ? "Bluetooth desactivado" : "Bluetooth activado",
                      "no se pudo cambiar el estado");
        panel_mark_dirty(p);
        return true;
    }
    if (y >= BT_PAD + BT_HEAD_H && y <= BT_PAD + BT_HEAD_H + 28.0 &&
        x >= bp->scan_x && x <= bp->scan_x + bp->scan_w) {
        if (!bp->scanning && bp->powered) {
            bt_schedule_scan(bp, true);
            bp->scanning = true;
            bp->scan_until = bt_now() + 15.0;
            bp->next_refresh = bt_now() + 2.0;
        } else if (bp->scanning) {
            bt_schedule_scan(bp, false);
            bp->scanning = false;
        }
        panel_mark_dirty(p);
        return true;
    }

    double top = bp->dev_y0;
    for (int i = 0; i < BT_VISIBLE; i++) {
        double ry = top + i * BT_ROW_H;
        if (y >= ry && y < ry + BT_ROW_H - 8) {
            int idx = bp->scroll + i;
            if (idx < bp->n_devs) {
                bp->sel = idx;
                panel_mark_dirty(p);
            }
            return true;
        }
    }

    if (bp->act_y > 0 && y >= bp->act_y && y < bp->act_y + BT_ACT_H) {
        if (bp->act_w1 > 0 && x >= bp->act_x1 && x < bp->act_x1 + bp->act_w1) {
            if (bt_primary(bp, bp->sel) == 0) panel_mark_dirty(p);
            return true;
        }
        if (bp->act_w2 > 0 && x >= bp->act_x2 && x < bp->act_x2 + bp->act_w2) {
            if (bp->sel >= 0 && bp->sel < bp->n_devs && bp->devs[bp->sel].paired) {
                bt_do(bp, bp->sel, "remove", 1.5, "Olvidando\xE2\x80\xA6",
                      "Dispositivo olvidado", "no se pudo olvidar");
                panel_mark_dirty(p);
            }
            return true;
        }
    }
    return true;
}

static void bt_motion(struct panel *p, double x, double y)
{
    struct bluetooth_panel *bp = p->userdata;
    int want = BT_HOVER_NONE;

    if (y >= BT_PAD && y <= BT_PAD + 28.0 && x >= bp->pwr_x &&
        x <= bp->pwr_x + bp->pwr_w)
        want = BT_HOVER_POWER;
    else if (y >= BT_PAD + BT_HEAD_H && y <= BT_PAD + BT_HEAD_H + 28.0 &&
             x >= bp->scan_x && x <= bp->scan_x + bp->scan_w)
        want = BT_HOVER_SCAN;
    else if (bp->act_y > 0 && y >= bp->act_y && y < bp->act_y + BT_ACT_H) {
        if (bp->act_w1 > 0 && x >= bp->act_x1 && x < bp->act_x1 + bp->act_w1)
            want = BT_HOVER_PRIMARY;
        else if (bp->act_w2 > 0 && x >= bp->act_x2 && x < bp->act_x2 + bp->act_w2)
            want = BT_HOVER_FORGET;
    } else {
        double top = bp->dev_y0;
        for (int i = 0; i < BT_VISIBLE; i++) {
            double ry = top + i * BT_ROW_H;
            if (y >= ry && y < ry + BT_ROW_H - 8) {
                if (bp->scroll + i < bp->n_devs) want = BT_HOVER_DEV_BASE + i;
                break;
            }
        }
    }

    if (want != bp->hover) {
        bp->hover = want;
        panel_mark_dirty(p);
    }
}

/* ---------------------------------------------------------------- lifecycle */

static void bt_show(struct bluetooth_panel *bp)
{
    bt_sel_ensure_visible(bp);
    /* the message line is always reserved so the surface never needs resizing */
    int h = (int)(BT_PAD + BT_HEAD_H + BT_STATUS_H + 6 + BT_SEC_GAP +
                  BT_VISIBLE * BT_ROW_H + 24 + 28 + BT_PAD);
    if (h < BT_MIN_H) h = BT_MIN_H;
    struct output *out = bp->panel.out ? bp->panel.out : bp->fallback_out;
    if (!panel_show_at(&bp->panel, out, BT_W, h, PANEL_ANCHOR_TOP_RIGHT))
        log_warn("bluetooth panel: show failed");
}

struct bluetooth_panel *bluetooth_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                               struct output *out)
{
    struct bluetooth_panel *bp = calloc(1, sizeof(*bp));
    if (!bp) return NULL;
    panel_init(&bp->panel, ctx);
    bp->cfg = cfg ? *cfg : NULL;
    bp->fallback_out = out;
    bp->hover = BT_HOVER_NONE;
    bp->sel = 0;
    panel_set_callbacks(&bp->panel, bt_draw, bt_key, bt_click, bt_motion, bp);
    log_info("bluetooth panel created");
    return bp;
}

void bluetooth_panel_destroy(struct bluetooth_panel *bp)
{
    if (!bp) return;
    if (bp->l_title) g_object_unref(bp->l_title);
    if (bp->l_label) g_object_unref(bp->l_label);
    if (bp->l_value) g_object_unref(bp->l_value);
    panel_fini(&bp->panel);
    free(bp);
}

struct panel *bluetooth_panel_surface(struct bluetooth_panel *bp)
{
    return bp ? &bp->panel : NULL;
}

void bluetooth_panel_open(struct bluetooth_panel *bp)
{
    if (!bp) return;
    bt_gather(bp);
    bp->hover = BT_HOVER_NONE;
    bt_show(bp);
}

void bluetooth_panel_toggle_proxy(void *userdata)
{
    struct bluetooth_panel *bp = userdata;
    if (!bp) return;
    if (panel_is_visible(&bp->panel)) {
        panel_hide(&bp->panel);
        return;
    }
    bluetooth_panel_open(bp);
}

void bluetooth_panel_scan(struct bluetooth_panel *bp)
{
    if (!bp) return;
    if (!bp->has_btctl) bt_gather(bp);
    if (!bp->powered) { if (panel_is_visible(&bp->panel)) panel_mark_dirty(&bp->panel); return; }
    if (!bp->scanning) {
        bt_schedule_scan(bp, true);
        bp->scanning = true;
        bp->scan_until = bt_now() + 15.0;
        bp->next_refresh = bt_now() + 2.0;
    } else {
        bp->next_refresh = bt_now() + 2.0;
    }
    if (panel_is_visible(&bp->panel)) panel_mark_dirty(&bp->panel);
}

int bluetooth_panel_select(struct bluetooth_panel *bp, int idx)
{
    if (!bp || idx < 0 || idx >= bp->n_devs) return -1;
    bp->sel = idx;
    if (panel_is_visible(&bp->panel)) panel_mark_dirty(&bp->panel);
    return 0;
}

int bluetooth_panel_primary_action(struct bluetooth_panel *bp, int idx)
{
    if (!bp) return -1;
    return bt_primary(bp, idx);
}

int bluetooth_panel_disconnect(struct bluetooth_panel *bp, int idx)
{
    if (!bp) return -1;
    return bt_do(bp, idx, "disconnect", 1.5, "Desconectando\xE2\x80\xA6",
                 "Desconectado", "no se pudo desconectar");
}

int bluetooth_panel_pair(struct bluetooth_panel *bp, int idx)
{
    if (!bp) return -1;
    if (idx < 0 || idx >= bp->n_devs) return -1;
    if (bp->has_btctl) {
        char cmd[1000];
        bt_cmd(bp, cmd, sizeof(cmd), "--agent NoInputNoOutput", "default-agent");
        bt_spawn(cmd);
    }
    return bt_do(bp, idx, "pair", 8.0, "Emparejando\xE2\x80\xA6",
                 "Emparejado", "no se pudo emparejar");
}

int bluetooth_panel_forget(struct bluetooth_panel *bp, int idx)
{
    if (!bp) return -1;
    return bt_do(bp, idx, "remove", 1.5, "Olvidando\xE2\x80\xA6",
                 "Dispositivo olvidado", "no se pudo olvidar");
}

void bluetooth_panel_tick(struct bluetooth_panel *bp)
{
    if (!bp) return;
    double now = bt_now();
    bool refresh = false;
    if (bp->scanning && now >= bp->scan_until) {
        bt_schedule_scan(bp, false);
        bp->scanning = false;
        refresh = true;
    }
    if (bp->pending && now >= bp->next_refresh) {
        bp->pending = false;
        bt_read_result(bp);
        refresh = true;
    }
    if (bp->scanning && now >= bp->next_refresh) {
        bp->next_refresh = now + 2.0;
        refresh = true;
    }
    if (refresh) {
        bt_gather(bp);
        if (panel_is_visible(&bp->panel)) panel_mark_dirty(&bp->panel);
    }
}

void bluetooth_panel_status(struct bluetooth_panel *bp, char *out, size_t n)
{
    if (!out || !n) return;
    if (!bp) { snprintf(out, n, "unavailable"); return; }
    const char *cur = "\xE2\x80\x94", *sel = "\xE2\x80\x94";
    for (int i = 0; i < bp->n_devs; i++) {
        if (bp->devs[i].connected) cur = bp->devs[i].name;
        if (i == bp->sel) sel = bp->devs[i].name;
    }
    snprintf(out, n,
             "tool=%s adapter=%s powered=%s discovering=%s devices=%d connected=%s "
             "sel=%s scanning=%d pending=%d msg=%s",
             bp->has_btctl ? "yes" : "no",
             bp->has_adapter ? (bp->adapter[0] ? bp->adapter : "ok") : "none",
             bp->powered ? "yes" : "no", bp->discovering ? "yes" : "no",
             bp->n_devs, cur, sel, bp->scanning ? 1 : 0, bp->pending ? 1 : 0,
             bp->msg[0] ? bp->msg : "\xE2\x80\x94");
}
