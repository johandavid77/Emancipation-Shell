#include "bar/backends.h"
#include "config/config.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double mono(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Run `cmd` through the shell and capture stdout. Returns true when the
 * command exited 0. Trailing whitespace is stripped. */
static bool run_capture(const char *cmd, char *out, size_t sz)
{
    if (sz == 0) return false;
    out[0] = '\0';
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    size_t n = fread(out, 1, sz - 1, p);
    out[n] = '\0';
    int rc = pclose(p);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ' || out[n - 1] == '\t'))
        out[--n] = '\0';
    return rc == 0;
}

static bool run_cmd(const char *cmd)
{
    return system(cmd) == 0;
}

/* ---- built-in module names ---------------------------------------------- */

static const char *known_modules[] = {
    "launcher", "workspaces", "taskbar", "clock", "date", "tray",
    "kbd", "keyboard", "keyboard_layout", "control_center", "weather",
    "volume", "network", "cpu", "ram", "battery", "media", "clipboard",
    "brightness", "power", "battery_percent", NULL
};

bool backends_is_builtin_module(const char *name)
{
    for (int i = 0; known_modules[i]; i++)
        if (strcmp(name, known_modules[i]) == 0) return true;
    return false;
}

/* ---- volume ------------------------------------------------------------- */

static int parse_first_pct(const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p == '%') {
            const char *q = p;
            while (q > s && q[-1] >= '0' && q[-1] <= '9') q--;
            if (q < p) return atoi(q);
        }
    }
    return -1;
}

static bool volume_probe(struct backends_state *b)
{
    char out[256];
    if (run_capture("command -v wpctl >/dev/null 2>&1 && wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", out, sizeof(out))) {
        snprintf(b->vol_backend, sizeof(b->vol_backend), "%s", "wpctl");
        return true;
    }
    if (run_capture("command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1", out, sizeof(out))) {
        snprintf(b->vol_backend, sizeof(b->vol_backend), "%s", "pactl");
        return true;
    }
    if (run_capture("command -v amixer >/dev/null 2>&1 && amixer get Master 2>/dev/null", out, sizeof(out))) {
        snprintf(b->vol_backend, sizeof(b->vol_backend), "%s", "amixer");
        return true;
    }
    return false;
}

static void read_volume(struct backends_state *b)
{
    char out[256];
    if (!b->vol_backend[0]) {
        if (!volume_probe(b)) {
            b->vol_available = false;
            return;
        }
    }

    if (strcmp(b->vol_backend, "wpctl") == 0) {
        if (!run_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", out, sizeof(out))) {
            b->vol_available = false;
            return;
        }
        const char *v = strstr(out, "Volume:");
        if (!v) { b->vol_available = false; return; }
        double f = atof(v + 7);
        b->vol_pct = (int)(f * 100.0 + 0.5);
        b->vol_muted = strstr(out, "[MUTED]") != NULL;
        b->vol_available = true;
    } else if (strcmp(b->vol_backend, "pactl") == 0) {
        if (!run_capture("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", out, sizeof(out))) {
            b->vol_available = false;
            return;
        }
        int pct = parse_first_pct(out);
        char mout[64];
        bool muted = run_capture("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", mout, sizeof(mout)) &&
                     strstr(mout, "yes") != NULL;
        b->vol_pct = pct;
        b->vol_muted = muted;
        b->vol_available = pct >= 0;
    } else { /* amixer */
        if (!run_capture("amixer get Master 2>/dev/null", out, sizeof(out))) {
            b->vol_available = false;
            return;
        }
        int pct = parse_first_pct(out);
        b->vol_pct = pct;
        b->vol_muted = strstr(out, "[off]") != NULL;
        b->vol_available = pct >= 0;
    }
}

void backends_volume_step(struct backends_state *b, int delta)
{
    char cmd[256];
    if (!b->vol_backend[0]) volume_probe(b);
    if (strcmp(b->vol_backend, "wpctl") == 0)
        snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%%%c 2>/dev/null",
                 delta < 0 ? -delta : delta, delta < 0 ? '-' : '+');
    else if (strcmp(b->vol_backend, "pactl") == 0)
        snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %s%d%% 2>/dev/null",
                 delta < 0 ? "-" : "+", delta < 0 ? -delta : delta);
    else if (strcmp(b->vol_backend, "amixer") == 0)
        snprintf(cmd, sizeof(cmd), "amixer -q set Master %d%%%c 2>/dev/null",
                 delta < 0 ? -delta : delta, delta < 0 ? '-' : '+');
    else
        return;
    run_cmd(cmd);
    read_volume(b);
}

void backends_volume_toggle_mute(struct backends_state *b)
{
    char cmd[256];
    if (!b->vol_backend[0]) volume_probe(b);
    if (strcmp(b->vol_backend, "wpctl") == 0)
        snprintf(cmd, sizeof(cmd), "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle 2>/dev/null");
    else if (strcmp(b->vol_backend, "pactl") == 0)
        snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ toggle 2>/dev/null");
    else if (strcmp(b->vol_backend, "amixer") == 0)
        snprintf(cmd, sizeof(cmd), "amixer -q set Master toggle 2>/dev/null");
    else
        return;
    run_cmd(cmd);
    read_volume(b);
}

/* ---- brightness --------------------------------------------------------- */

static void probe_backlight(struct backends_state *b)
{
    b->br_available = false;
    b->bl_path[0] = '\0';
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[320];
        snprintf(p, sizeof(p), "/sys/class/backlight/%s/max_brightness", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        fclose(f);
        snprintf(b->bl_path, sizeof(b->bl_path), "/sys/class/backlight/%s", e->d_name);
        b->br_available = true;
        break;
    }
    closedir(d);
}

static void read_brightness(struct backends_state *b)
{
    if (!b->bl_path[0]) probe_backlight(b);
    b->br_available = b->bl_path[0] != '\0';
    if (!b->br_available) return;
    char p[360], buf[32];
    int cur = -1, max = -1;
    snprintf(p, sizeof(p), "%s/brightness", b->bl_path);
    FILE *f = fopen(p, "r");
    if (!f) { b->br_available = false; return; }
    if (fscanf(f, "%d", &cur) != 1) cur = -1;
    fclose(f);
    snprintf(p, sizeof(p), "%s/max_brightness", b->bl_path);
    f = fopen(p, "r");
    if (!f) { b->br_available = false; return; }
    if (fscanf(f, "%d", &max) != 1) max = -1;
    fclose(f);
    if (cur < 0 || max <= 0) { b->br_available = false; return; }
    b->br_pct = (int)(100.0 * cur / max + 0.5);
}

void backends_brightness_step(struct backends_state *b, int delta)
{
    if (!b->bl_path[0]) probe_backlight(b);
    if (!b->br_available) return;
    char p[360];
    int max = 100;
    snprintf(p, sizeof(p), "%s/max_brightness", b->bl_path);
    FILE *f = fopen(p, "r");
    if (f) { if (fscanf(f, "%d", &max) != 1) max = 100; fclose(f); }
    int cur = b->br_pct * max / 100;
    cur += delta * max / 100;
    if (cur < 1) cur = 1;
    if (cur > max) cur = max;
    snprintf(p, sizeof(p), "%s/brightness", b->bl_path);
    f = fopen(p, "w");
    if (f) { fprintf(f, "%d", cur); fclose(f); }
    read_brightness(b);
}

/* ---- network ------------------------------------------------------------ */

static void read_network(struct backends_state *b)
{
    b->net_kind = NET_NONE;
    b->net_connected = false;
    b->net_if[0] = '\0';
    b->net_ssid[0] = '\0';
    b->net_strength = -1;

    DIR *d = opendir("/sys/class/net");
    if (!d) return;
    struct dirent *e;
    char best[32] = "";
    enum net_kind best_kind = NET_NONE;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strcmp(e->d_name, "lo") == 0) continue;
        char p[300], state[32] = "";
        snprintf(p, sizeof(p), "/sys/class/net/%s/operstate", e->d_name);
        FILE *f = fopen(p, "r");
        if (f) { if (!fgets(state, sizeof(state), f)) state[0] = '\0'; fclose(f); }
        bool up = strncmp(state, "up", 2) == 0;
        if (!up) continue;
        snprintf(p, sizeof(p), "/sys/class/net/%s/wireless", e->d_name);
        bool wifi = access(p, F_OK) == 0;
        enum net_kind k = wifi ? NET_WIFI : (strncmp(e->d_name, "eth", 3) == 0 || strncmp(e->d_name, "en", 2) == 0 ? NET_ETHERNET : NET_OTHER);
        if (best[0] == '\0' || (k == NET_WIFI && best_kind != NET_WIFI)) {
            snprintf(best, sizeof(best), "%s", e->d_name);
            best_kind = k;
        }
    }
    closedir(d);

    if (best[0] == '\0') return;
    snprintf(b->net_if, sizeof(b->net_if), "%s", best);
    b->net_kind = best_kind;
    b->net_connected = true;

    if (best_kind == NET_WIFI) {
        char out[128];
        /* Prefer NetworkManager when it is installed; iwgetid is the fallback
         * on minimal rootfs without nmcli. */
        static const char *nmcli_ssid =
            "command -v nmcli >/dev/null 2>&1 && "
            "nmcli -t -f ACTIVE,SSID dev wifi 2>/dev/null | grep '^yes:' | head -n1 | cut -d: -f2-";
        if (run_capture(nmcli_ssid, out, sizeof(out)) && out[0]) {
            /* terse mode escapes ':' inside SSIDs as '\:' */
            char dst[64];
            size_t o = 0;
            for (char *p = out; *p && o + 1 < sizeof(dst); p++) {
                if (*p == '\\' && p[1]) p++;
                dst[o++] = *p;
            }
            dst[o] = '\0';
            snprintf(b->net_ssid, sizeof(b->net_ssid), "%s", dst);
        } else if (run_capture("command -v iwgetid >/dev/null 2>&1 && iwgetid -r 2>/dev/null",
                                out, sizeof(out)) && out[0]) {
            snprintf(b->net_ssid, sizeof(b->net_ssid), "%.60s", out);
        }
        /* quality from /proc/net/wireless */
        FILE *f = fopen("/proc/net/wireless", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                char ifc[32];
                double q;
                if (sscanf(line, " %31[^:]: %*d %lf", ifc, &q) == 2 && strcmp(ifc, best) == 0) {
                    int pct = (int)(q * 100.0 / 70.0 + 0.5);
                    if (pct > 100) pct = 100;
                    if (pct < 0) pct = 0;
                    b->net_strength = pct;
                }
            }
            fclose(f);
        }
    }
}

/* ---- script-backed custom widgets --------------------------------------- */

static void refresh_scripts(struct backends_state *b, const struct config *cfg, bool *changed)
{
    if (!cfg) return;
    double now = mono();
    for (int sec = 0; sec < BAR_SECTIONS; sec++) {
        for (int i = 0; i < cfg->bar.n_modules[sec]; i++) {
            const char *name = cfg->bar.modules[sec][i];
            if (!name[0] || backends_is_builtin_module(name)) continue;
            struct script_widget *sw = NULL;
            for (int j = 0; j < b->n_scripts; j++)
                if (strcmp(b->scripts[j].cmd, name) == 0) { sw = &b->scripts[j]; break; }
            if (!sw) {
                if (b->n_scripts >= BACKENDS_MAX_SCRIPTS) continue;
                sw = &b->scripts[b->n_scripts++];
                memset(sw, 0, sizeof(*sw));
                snprintf(sw->cmd, sizeof(sw->cmd), "%s", name);
                sw->interval = 5;
            }
            if (!sw->ran || now - sw->last >= sw->interval) {
                char out[BACKENDS_SCRIPT_OUT];
                if (run_capture(sw->cmd, out, sizeof(out))) {
                    if (strcmp(out, sw->out) != 0) { snprintf(sw->out, sizeof(sw->out), "%s", out); *changed = true; }
                }
                sw->last = now;
                sw->ran = true;
            }
        }
    }
}

const char *backends_script_output(const struct backends_state *b, const char *cmd)
{
    for (int j = 0; j < b->n_scripts; j++)
        if (strcmp(b->scripts[j].cmd, cmd) == 0 && b->scripts[j].out[0])
            return b->scripts[j].out;
    return NULL;
}

/* ---- lifecycle ---------------------------------------------------------- */

void backends_init(struct backends_state *b)
{
    memset(b, 0, sizeof(*b));
    b->vol_pct = -1;
    b->br_pct = -1;
    b->net_strength = -1;
    b->net_kind = NET_NONE;
}

bool backends_update(struct backends_state *b, const struct config *cfg, int clipboard_count)
{
    bool changed = false;
    double now = mono();

    if (b->clipboard_count != clipboard_count) {
        b->clipboard_count = clipboard_count;
        changed = true;
    }

    if (now - b->t_fast >= 2.0) {
        b->t_fast = now;
        int pv = b->vol_pct; bool pm = b->vol_muted; bool pa = b->vol_available;
        read_volume(b);
        if (pv != b->vol_pct || pm != b->vol_muted || pa != b->vol_available) changed = true;

        int pb = b->br_pct; bool pba = b->br_available;
        read_brightness(b);
        if (pb != b->br_pct || pba != b->br_available) changed = true;

        enum net_kind nk = b->net_kind; bool nc = b->net_connected;
        int ns = b->net_strength; char nsid[64]; snprintf(nsid, sizeof(nsid), "%s", b->net_ssid);
        read_network(b);
        if (nk != b->net_kind || nc != b->net_connected || ns != b->net_strength ||
            strcmp(nsid, b->net_ssid) != 0)
            changed = true;
    }

    refresh_scripts(b, cfg, &changed);
    return changed;
}
