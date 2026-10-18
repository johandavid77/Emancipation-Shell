#include "control/network_panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "bar/backends.h"
#include "util/log.h"
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>

#define NP_W 340
#define NP_H 400
#define NP_PAD 18.0
#define NP_TAB_H 30.0
#define NP_TAB_GAP 8.0
#define NP_ROW_H 26.0
#define NP_AP_ROW_H 42.0
#define NP_AP_MAX 64
#define NP_AP_VISIBLE 6
#define NP_PW_H 34.0
#define NP_BTN_H 32.0

#define NP_WIFI_RESULT "/tmp/emancipation-wifi.result"

enum {
    NP_HOVER_NONE = -1,
    NP_HOVER_WIFI = 1,       /* Wi-Fi radio toggle on the details page */
    NP_HOVER_TAB_DETAILS = 2,
    NP_HOVER_TAB_WIFI = 3,
    NP_HOVER_DISCONNECT = 4,
    NP_HOVER_AP_BASE = 100,
};

#define NP_VIEW_DETAILS 0
#define NP_VIEW_WIFI 1

/* Snapshot of everything the popup shows; refreshed every time it is opened. */
struct net_details {
    char iface[32];
    char kind_lbl[16];      /* "Ethernet" / "Wi-Fi" / "Otro" */
    enum net_kind kind;
    char state[24];         /* operstate: up / down / ... */
    char ssid[64];
    int strength;           /* 0..100, -1 unknown */
    int dbm;                /* dBm, INT_MIN unknown */
    char ipv4[48];          /* a.b.c.d/len */
    char netmask[32];
    char gateway[64];
    char dns[2][64];
    int ndns;
    char mac[32];
    int speed;              /* Mb/s, -1 unknown */
    int mtu;
    bool wifi_radio;        /* nmcli radio wifi */
    bool has_wifi_radio;
    char wifi_iface[32];    /* first wireless interface, if any */
};

struct np_ap {
    char ssid[64];
    int signal;             /* 0..100 */
    char security[28];      /* e.g. "WPA2", "" / "--" for open */
    bool in_use;
    bool secure;
};

struct network_panel {
    struct panel panel;
    const struct config *cfg;
    struct output *fallback_out;
    const struct backends_state *bk;
    struct net_details d;
    PangoLayout *l_title;
    PangoLayout *l_label;
    PangoLayout *l_value;

    int hover;
    int view;               /* NP_VIEW_* */
    double wifi_row_y;      /* radio toggle geometry (details page) */

    char nmcli[512];
    bool has_nmcli;

    struct np_ap aps[NP_AP_MAX];
    int n_aps;
    int sel;                /* selected AP index */
    int scroll;             /* first visible AP index */
    double disc_y;          /* disconnect button top, 0 when hidden */

    bool pw_prompt;
    int pw_ap;
    char pw[128];
    int pw_len;

    bool connecting;
    char connecting_ssid[64];
    char msg[200];
    int msg_kind;           /* 0 none, 1 info, 2 error, 3 ok */
};

/* ------------------------------------------------------------------ probing */

/* Read the first line of a file (e.g. /sys entries) without spawning a shell:
 * the minimal Emancipation rootfs ships neither iproute2 nor busybox. */
static bool np_read_line(const char *path, char *out, size_t n)
{
    if (out && n) out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(out, (int)n, f)) { fclose(f); return false; }
    fclose(f);
    size_t r = strlen(out);
    while (r && isspace((unsigned char)out[r - 1])) out[--r] = '\0';
    return true;
}

/* Run a command and capture its (trimmed) output; used only for nmcli, which
 * is absent on the minimal rootfs. */
static bool np_run(const char *cmd, char *out, size_t n)
{
    if (out && n) out[0] = '\0';
    FILE *f = popen(cmd, "r");
    if (!f) return false;
    size_t r = out ? fread(out, 1, n - 1, f) : 0;
    if (out) out[r] = '\0';
    int rc = pclose(f);
    if (out) {
        size_t k = strlen(out);
        while (k && isspace((unsigned char)out[k - 1])) out[--k] = '\0';
    }
    return rc == 0;
}

/* Locate an external tool across the usual bin dirs (plus ~/.local/bin). */
static bool np_tool_path(const char *name, char *out, size_t n)
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

/* Interface names come from the kernel; reject anything that could break the
 * paths we build from them. */
static bool np_if_safe(const char *ifn)
{
    if (!ifn || !*ifn || strlen(ifn) >= 16) return false;
    for (const char *p = ifn; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') return false;
    }
    return true;
}

/* First interface whose /sys/class/net/<if>/wireless directory exists. */
static bool np_first_wifi_iface(char *out, size_t n)
{
    DIR *d = opendir("/sys/class/net");
    if (!d) return false;
    struct dirent *e;
    bool ok = false;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[320];
        snprintf(p, sizeof(p), "/sys/class/net/%.200s/wireless", e->d_name);
        if (access(p, F_OK) == 0) { snprintf(out, n, "%s", e->d_name); ok = true; break; }
    }
    closedir(d);
    return ok;
}

/* IPv4 address + netmask for `iface` (or the first non-loopback interface with
 * an address when `iface` is empty) via getifaddrs(). */
static bool np_ifaddr(const char *want, char *iface, size_t ifn, char *ip, size_t ipn,
                      char *mask, size_t mkn)
{
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return false;
    bool found = false;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(a->ifa_name, "lo") == 0) continue;
        if (want && want[0] && strcmp(a->ifa_name, want) != 0) continue;
        const struct sockaddr_in *sin = (const struct sockaddr_in *)a->ifa_addr;
        const struct sockaddr_in *sm = (const struct sockaddr_in *)a->ifa_netmask;
        inet_ntop(AF_INET, &sin->sin_addr, ip, (socklen_t)ipn);
        if (sm) inet_ntop(AF_INET, &sm->sin_addr, mask, (socklen_t)mkn);
        snprintf(iface, ifn, "%s", a->ifa_name);
        found = true;
        break;
    }
    freeifaddrs(list);
    return found;
}

/* Default gateway from /proc/net/route (hex, little-endian). */
static bool np_gateway(const char *iface, char *out, size_t n)
{
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return false;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return false; } /* header */
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        char ifn[32];
        unsigned long dest = 0, gw = 0;
        if (sscanf(line, "%31s %lx %lx", ifn, &dest, &gw) != 3) continue;
        if (dest != 0) continue; /* only the default route */
        if (iface && iface[0] && strcmp(ifn, iface) != 0) continue;
        snprintf(out, n, "%lu.%lu.%lu.%lu", gw & 255, (gw >> 8) & 255, (gw >> 16) & 255, (gw >> 24) & 255);
        ok = true;
        break;
    }
    fclose(f);
    return ok;
}

/* DNS servers from /etc/resolv.conf. */
static int np_dns(char *a, size_t an, char *b, size_t bn)
{
    FILE *f = fopen("/etc/resolv.conf", "r");
    if (!f) return 0;
    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "nameserver", 10) != 0) continue;
        char *p = line + 10;
        while (*p == ' ' || *p == '\t') p++;
        char *e = p;
        while (*e && !isspace((unsigned char)*e)) e++;
        *e = '\0';
        if (!*p) continue;
        if (count == 0) snprintf(a, an, "%s", p);
        else if (count == 1) snprintf(b, bn, "%s", p);
        count++;
        if (count >= 2) break;
    }
    fclose(f);
    return count;
}

/* Single-quote a string for safe use inside `sh -c`. */
static void np_shell_quote(const char *in, char *out, size_t n)
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

static void np_gather(struct network_panel *np)
{
    struct net_details *d = &np->d;
    memset(d, 0, sizeof(*d));
    d->strength = -1;
    d->speed = -1;
    d->mtu = -1;
    d->dbm = -999;
    d->kind = NET_NONE;

    /* nmcli + Wi-Fi radio state are probed independently of the active link so
     * the Wi-Fi page works even when nothing is connected. */
    np->has_nmcli = np_tool_path("nmcli", np->nmcli, sizeof(np->nmcli));
    np_first_wifi_iface(d->wifi_iface, sizeof(d->wifi_iface));
    if (np->has_nmcli) {
        char cmd[640], rb[64];
        char q[600];
        np_shell_quote(np->nmcli, q, sizeof(q));
        snprintf(cmd, sizeof(cmd), "%s -t radio wifi 2>/dev/null", q);
        if (np_run(cmd, rb, sizeof(rb)) && rb[0]) {
            d->has_wifi_radio = true;
            d->wifi_radio = strstr(rb, "enabled") != NULL;
        }
    }

    /* Prefer the interface the bar backend already selected. */
    char want[32] = "";
    if (np->bk && np->bk->net_if[0] && np_if_safe(np->bk->net_if)) {
        snprintf(want, sizeof(want), "%s", np->bk->net_if);
        d->kind = np->bk->net_kind;
        snprintf(d->ssid, sizeof(d->ssid), "%s", np->bk->net_ssid);
        d->strength = np->bk->net_strength;
    }

    char iface[32] = "", ip[INET_ADDRSTRLEN] = "", mask[INET_ADDRSTRLEN] = "";
    if (np_ifaddr(want, iface, sizeof(iface), ip, sizeof(ip), mask, sizeof(mask))) {
        snprintf(d->iface, sizeof(d->iface), "%s", iface);
        if (ip[0]) snprintf(d->ipv4, sizeof(d->ipv4), "%s", ip);
        if (mask[0]) {
            snprintf(d->netmask, sizeof(d->netmask), "%s", mask);
            /* compress to a /len suffix next to the address */
            struct in_addr m;
            if (inet_pton(AF_INET, mask, &m) == 1) {
                unsigned long bits = ntohl(m.s_addr);
                int len = 0;
                while (bits & 0x80000000UL) { len++; bits <<= 1; }
                if (d->ipv4[0]) snprintf(d->ipv4 + strlen(d->ipv4), sizeof(d->ipv4) - strlen(d->ipv4), "/%d", len);
            }
        }
    } else if (np->bk && np->bk->net_if[0]) {
        snprintf(d->iface, sizeof(d->iface), "%s", np->bk->net_if);
    }

    if (!d->iface[0] || !np_if_safe(d->iface)) {
        snprintf(d->state, sizeof(d->state), "sin interfaz");
        snprintf(d->kind_lbl, sizeof(d->kind_lbl), "%s", "\xE2\x80\x94");
        return;
    }

    if (d->kind == NET_NONE) {
        char wp[160];
        snprintf(wp, sizeof(wp), "/sys/class/net/%s/wireless", d->iface);
        bool wifi = access(wp, F_OK) == 0;
        if (strncmp(d->iface, "eth", 3) == 0 || strncmp(d->iface, "en", 2) == 0) d->kind = NET_ETHERNET;
        else if (wifi) d->kind = NET_WIFI;
        else d->kind = NET_OTHER;
    }

    snprintf(d->kind_lbl, sizeof(d->kind_lbl), "%s",
             d->kind == NET_WIFI ? "Wi-Fi" : d->kind == NET_ETHERNET ? "Ethernet" : "Otro");

    char path[192], buf[128];

    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", d->iface);
    char sbuf[24];
    if (np_read_line(path, sbuf, sizeof(sbuf))) snprintf(d->state, sizeof(d->state), "%s", sbuf);
    else snprintf(d->state, sizeof(d->state), "desconocido");

    snprintf(path, sizeof(path), "/sys/class/net/%s/address", d->iface);
    np_read_line(path, d->mac, sizeof(d->mac));

    snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", d->iface);
    if (np_read_line(path, buf, sizeof(buf))) d->mtu = atoi(buf);

    snprintf(path, sizeof(path), "/sys/class/net/%s/speed", d->iface);
    if (np_read_line(path, buf, sizeof(buf)) && buf[0]) d->speed = atoi(buf);

    if (!d->ipv4[0]) {
        char ipb[INET_ADDRSTRLEN] = "", mb[INET_ADDRSTRLEN] = "", ib[32] = "";
        if (np_ifaddr(d->iface, ib, sizeof(ib), ipb, sizeof(ipb), mb, sizeof(mb))) {
            if (ipb[0]) snprintf(d->ipv4, sizeof(d->ipv4), "%s", ipb);
            if (mb[0]) snprintf(d->netmask, sizeof(d->netmask), "%s", mb);
        }
    }

    if (!np_gateway(d->iface, d->gateway, sizeof(d->gateway)))
        np_gateway(NULL, d->gateway, sizeof(d->gateway));

    d->ndns = np_dns(d->dns[0], sizeof(d->dns[0]), d->dns[1], sizeof(d->dns[1]));

    if (d->kind == NET_WIFI) {
        FILE *f = fopen("/proc/net/wireless", "r");
        if (f) {
            char line[256], ifc[32];
            double link, level;
            while (fgets(line, sizeof(line), f)) {
                if (sscanf(line, " %31[^:]: %*d %lf %lf", ifc, &link, &level) == 3 &&
                    strcmp(ifc, d->iface) == 0) {
                    d->dbm = (int)level;
                    if (d->strength < 0) {
                        int pct = (int)((link - 100.0) * 100.0 / 70.0 + 0.5);
                        if (pct > 100) pct = 100;
                        if (pct < 0) pct = 0;
                        d->strength = pct;
                    }
                    break;
                }
            }
            fclose(f);
        }
    }
}

/* ---------------------------------------------------------- wifi AP scanning */

static int np_split_escaped(const char *line, char f[][160], int maxf)
{
    int fi = 0, o = 0;
    for (const char *p = line;; p++) {
        char c = *p;
        if (c == '\\' && p[1]) { p++; c = *p; }
        else if (c == ':' || c == '\0') {
            if (fi < maxf) f[fi][o] = '\0';
            fi++;
            o = 0;
            if (c == '\0' || fi >= maxf) break;
            continue;
        }
        if (fi < maxf && o < 159) f[fi][o++] = c;
    }
    return fi;
}

static void np_ap_add(struct network_panel *np, const char *ssid, int sig, bool in_use,
                      bool secure, const char *sec)
{
    if (!ssid || !ssid[0]) return;
    for (int i = 0; i < np->n_aps; i++) {
        if (strcmp(np->aps[i].ssid, ssid) == 0) {
            if (sig > np->aps[i].signal) np->aps[i].signal = sig;
            if (in_use) np->aps[i].in_use = true;
            if (secure) np->aps[i].secure = true;
            if (sec[0] && !np->aps[i].security[0])
                snprintf(np->aps[i].security, sizeof(np->aps[i].security), "%s", sec);
            return;
        }
    }
    if (np->n_aps >= NP_AP_MAX) return;
    struct np_ap *a = &np->aps[np->n_aps++];
    snprintf(a->ssid, sizeof(a->ssid), "%s", ssid);
    a->signal = sig;
    a->in_use = in_use;
    a->secure = secure;
    snprintf(a->security, sizeof(a->security), "%s", sec);
}

static void np_ap_sort(struct network_panel *np)
{
    /* in-use first, then strongest signal */
    for (int i = 0; i < np->n_aps; i++) {
        for (int j = i + 1; j < np->n_aps; j++) {
            bool swap = false;
            if (np->aps[j].in_use != np->aps[i].in_use) swap = np->aps[j].in_use;
            else if (np->aps[j].signal > np->aps[i].signal) swap = true;
            if (swap) {
                struct np_ap t = np->aps[i];
                np->aps[i] = np->aps[j];
                np->aps[j] = t;
            }
        }
    }
}

static void np_wifi_scan(struct network_panel *np)
{
    np->n_aps = 0;
    if (!np->has_nmcli) return;
    char cmd[1024], buf[8192];
    char q[600];
    np_shell_quote(np->nmcli, q, sizeof(q));
    snprintf(cmd, sizeof(cmd),
             "%s -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list 2>/dev/null", q);
    np_run(cmd, buf, sizeof(buf));

    char *save = buf;
    while (*save) {
        char *nl = strchr(save, '\n');
        if (nl) *nl = '\0';
        if (*save) {
            char f[4][160];
            int nf = np_split_escaped(save, f, 4);
            if (nf >= 4 && f[1][0]) {
                int sig = atoi(f[2]);
                bool in_use = (f[0][0] == '*');
                bool secure = !(f[3][0] == '\0' ||
                                (f[3][0] == '-' && f[3][1] == '-' && f[3][2] == '\0'));
                np_ap_add(np, f[1], sig, in_use, secure, f[3]);
            }
        }
        if (!nl) break;
        save = nl + 1;
    }
    np_ap_sort(np);
    if (np->sel >= np->n_aps) np->sel = np->n_aps > 0 ? np->n_aps - 1 : 0;
    if (np->sel < 0) np->sel = 0;
}

static bool np_ap_in_use(const struct network_panel *np)
{
    for (int i = 0; i < np->n_aps; i++)
        if (np->aps[i].in_use) return true;
    return false;
}

/* Preferred surface height for the current state; the panel is resized
 * whenever it changes (e.g. the password row appearing). */
static int np_height(const struct network_panel *np)
{
    int rows = 3;
    if (np->d.kind == NET_WIFI) rows += 2;
    if (np->d.netmask[0]) rows += 1;
    if (np->d.ndns > 1) rows += 1;
    if (np->d.speed > 0) rows += 1;
    if (np->d.mtu > 0) rows += 1;
    int h = (int)(NP_PAD + NP_TAB_H + 10 + rows * NP_ROW_H +
                  (np->d.has_wifi_radio ? 46 : 8) + NP_PAD);
    if (np->d.has_wifi_radio) {
        int wifi_h = (int)(NP_PAD + NP_TAB_H + 10 + NP_AP_VISIBLE * NP_AP_ROW_H);
        if (np_ap_in_use(np)) wifi_h += 4 + NP_BTN_H + 6;
        if (np->pw_prompt) wifi_h += (int)(NP_PW_H + 4 + 18);
        wifi_h += 20 + NP_PAD;
        if (wifi_h > h) h = wifi_h;
    }
    return h < NP_H ? NP_H : h;
}

static void np_sync_size(struct network_panel *np)
{
    if (!panel_is_visible(&np->panel)) return;
    panel_resize(&np->panel, NP_W, np_height(np));
}

/* --------------------------------------------------------------- actions */

static void np_spawn(const char *cmd)
{
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int dn = open("/dev/null", O_RDONLY);
    if (dn >= 0) { dup2(dn, 0); if (dn > 2) close(dn); }
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    _exit(127);
}

static void np_wifi_connect(struct network_panel *np, const struct np_ap *ap, const char *pw)
{
    /* `pw` may alias np->pw: snapshot it before resetting the prompt state. */
    char pass[128];
    size_t pl = 0;
    if (pw && pw[0]) {
        pl = strlen(pw);
        if (pl >= sizeof(pass)) pl = sizeof(pass) - 1;
        memcpy(pass, pw, pl);
    }
    pass[pl] = '\0';

    np->pw_prompt = false;
    np->pw[0] = '\0';
    np->pw_len = 0;
    np_sync_size(np);
    if (!np->has_nmcli) return;
    unlink(NP_WIFI_RESULT);
    char qnm[600], qssid[200], qpw[300], cmd[1400];
    np_shell_quote(np->nmcli, qnm, sizeof(qnm));
    np_shell_quote(ap->ssid, qssid, sizeof(qssid));
    np->connecting = true;
    snprintf(np->connecting_ssid, sizeof(np->connecting_ssid), "%s", ap->ssid);
    if (ap->secure && pass[0]) {
        np_shell_quote(pass, qpw, sizeof(qpw));
        snprintf(cmd, sizeof(cmd),
                 "%s dev wifi connect %s password %s > " NP_WIFI_RESULT " 2>&1; "
                 "echo \"RC=$?\" >> " NP_WIFI_RESULT,
                 qnm, qssid, qpw);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "%s dev wifi connect %s > " NP_WIFI_RESULT " 2>&1; "
                 "echo \"RC=$?\" >> " NP_WIFI_RESULT,
                 qnm, qssid);
    }
    np_spawn(cmd);
    snprintf(np->msg, sizeof(np->msg), "Conectando a %s\xE2\x80\xA6", ap->ssid);
    np->msg_kind = 1;
}

static void np_wifi_disconnect(struct network_panel *np)
{
    if (!np->has_nmcli) return;
    const char *ifc = np->d.wifi_iface[0] ? np->d.wifi_iface : np->d.iface;
    if (!np_if_safe(ifc)) return;
    unlink(NP_WIFI_RESULT);
    char qnm[600], cmd[800];
    np_shell_quote(np->nmcli, qnm, sizeof(qnm));
    snprintf(cmd, sizeof(cmd),
             "%s dev disconnect %s > " NP_WIFI_RESULT " 2>&1; "
             "echo \"RC=$?\" >> " NP_WIFI_RESULT,
             qnm, ifc);
    np_spawn(cmd);
    np->connecting = true;
    np->connecting_ssid[0] = '\0';
    snprintf(np->msg, sizeof(np->msg), "Desconectando\xE2\x80\xA6");
    np->msg_kind = 1;
}

static void np_wifi_select(struct network_panel *np, int idx)
{
    if (idx < 0 || idx >= np->n_aps) return;
    np->sel = idx;
    struct np_ap *a = &np->aps[idx];
    if (a->in_use) return;
    if (a->secure) {
        np->pw_prompt = true;
        np->pw_ap = idx;
        np->pw_len = 0;
        np->pw[0] = '\0';
        np->msg[0] = '\0';
        np->msg_kind = 0;
        np_sync_size(np);
    } else {
        np_wifi_connect(np, a, NULL);
    }
}

void network_panel_tick(struct network_panel *np)
{
    if (!np || !np->connecting) return;
    FILE *f = fopen(NP_WIFI_RESULT, "r");
    if (!f) return;
    char buf[1024];
    size_t r = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[r] = '\0';
    unlink(NP_WIFI_RESULT);

    int rc = -1;
    char *rcp = strstr(buf, "RC=");
    if (rcp) rc = atoi(rcp + 3);

    np->connecting = false;
    if (rc == 0) {
        if (np->connecting_ssid[0])
            snprintf(np->msg, sizeof(np->msg), "Conectado a %s", np->connecting_ssid);
        else
            snprintf(np->msg, sizeof(np->msg), "Desconectado");
        np->msg_kind = 3;
    } else {
        /* keep the last meaningful nmcli line as the error text */
        char err[160] = "";
        char *p = buf;
        while (*p) {
            char *nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            if (*p && strncmp(p, "RC=", 3) != 0)
                snprintf(err, sizeof(err), "%.120s", p);
            if (!nl) break;
            p = nl + 1;
        }
        snprintf(np->msg, sizeof(np->msg), "Error: %s", err[0] ? err : "no se pudo conectar");
        np->msg_kind = 2;
    }
    np_wifi_scan(np);
    if (panel_is_visible(&np->panel)) panel_mark_dirty(&np->panel);
}

/* ------------------------------------------------------------------- drawing */

static void np_round(cairo_t *cr, double x, double y, double w, double h, double r)
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

static void np_lock_glyph(cairo_t *cr, double x, double y, double s, struct color_rgba c)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.85);
    np_round(cr, x, y + s * 0.45, s, s * 0.55, 2);
    cairo_fill(cr);
    cairo_set_line_width(cr, s * 0.14);
    cairo_arc(cr, x + s / 2, y + s * 0.5, s * 0.28, G_PI, 2 * G_PI);
    cairo_stroke(cr);
}

static void np_row(struct network_panel *np, cairo_t *cr, double y, const char *label,
                   const char *value, const struct theme_config *th)
{
    pango_layout_set_text(np->l_label, label, -1);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 0.9);
    cairo_move_to(cr, NP_PAD, y);
    pango_cairo_show_layout(cr, np->l_label);

    pango_layout_set_text(np->l_value, value ? value : "\xE2\x80\x94", -1);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    int vw = 0, vh = 0;
    pango_layout_get_pixel_size(np->l_value, &vw, &vh);
    cairo_move_to(cr, NP_W - NP_PAD - vw, y);
    pango_layout_set_width(np->l_value, -1);
    pango_cairo_show_layout(cr, np->l_value);
}

static double np_content_top(void)
{
    return NP_PAD + NP_TAB_H + 10;
}

static void np_draw_tabs(struct network_panel *np, cairo_t *cr, const struct theme_config *th, int w)
{
    if (!np->d.has_wifi_radio) {
        pango_layout_set_text(np->l_title, "Red", -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
        cairo_move_to(cr, NP_PAD, NP_PAD + 4);
        pango_cairo_show_layout(cr, np->l_title);
        return;
    }
    double tw = (w - 2 * NP_PAD - NP_TAB_GAP) / 2.0;
    const char *labels[2] = { "Detalles", "Wi-Fi" };
    for (int i = 0; i < 2; i++) {
        double x = NP_PAD + i * (tw + NP_TAB_GAP);
        bool active = (np->view == i);
        bool hov = (np->hover == (i == 0 ? NP_HOVER_TAB_DETAILS : NP_HOVER_TAB_WIFI));
        np_round(cr, x, NP_PAD, tw, NP_TAB_H, 9);
        if (active)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, hov ? 1.0 : 0.85);
        cairo_fill(cr);
        struct color_rgba ink = active ? th->on_primary : th->on_surface_variant;
        pango_layout_set_text(np->l_label, labels[i], -1);
        int tw2 = 0, th2 = 0;
        pango_layout_get_pixel_size(np->l_label, &tw2, &th2);
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, 0.95);
        cairo_move_to(cr, x + (tw - tw2) / 2, NP_PAD + 8);
        pango_cairo_show_layout(cr, np->l_label);
    }
}

static void np_draw_details(struct network_panel *np, cairo_t *cr, const struct theme_config *th, int w)
{
    double y = np_content_top();
    char v[128];

    np_row(np, cr, y, "Interfaz", np->d.iface[0] ? np->d.iface : "\xE2\x80\x94", th);
    y += NP_ROW_H;
    np_row(np, cr, y, "Tipo", np->d.kind_lbl, th);
    y += NP_ROW_H;
    snprintf(v, sizeof(v), "%s", np->d.state[0] ? np->d.state : "\xE2\x80\x94");
    np_row(np, cr, y, "Estado", v, th);
    y += NP_ROW_H;

    if (np->d.kind == NET_WIFI) {
        np_row(np, cr, y, "SSID", np->d.ssid[0] ? np->d.ssid : "\xE2\x80\x94", th);
        y += NP_ROW_H;
        if (np->d.strength >= 0) {
            if (np->d.dbm > -999) snprintf(v, sizeof(v), "%d%%  (%d dBm)", np->d.strength, np->d.dbm);
            else snprintf(v, sizeof(v), "%d%%", np->d.strength);
        } else snprintf(v, sizeof(v), "\xE2\x80\x94");
        np_row(np, cr, y, "Se\xC3\xB1" "al", v, th);
        y += NP_ROW_H;
    }

    np_row(np, cr, y, "IP (IPv4)", np->d.ipv4[0] ? np->d.ipv4 : "\xE2\x80\x94", th);
    y += NP_ROW_H;
    if (np->d.netmask[0]) {
        np_row(np, cr, y, "M\xC3\xA1scara", np->d.netmask, th);
        y += NP_ROW_H;
    }
    np_row(np, cr, y, "Puerta de enlace", np->d.gateway[0] ? np->d.gateway : "\xE2\x80\x94", th);
    y += NP_ROW_H;
    np_row(np, cr, y, "DNS", np->d.ndns ? np->d.dns[0] : "\xE2\x80\x94", th);
    y += NP_ROW_H;
    if (np->d.ndns > 1) {
        np_row(np, cr, y, "DNS 2", np->d.dns[1], th);
        y += NP_ROW_H;
    }
    np_row(np, cr, y, "MAC", np->d.mac[0] ? np->d.mac : "\xE2\x80\x94", th);
    y += NP_ROW_H;
    if (np->d.speed > 0) {
        snprintf(v, sizeof(v), "%d Mb/s", np->d.speed);
        np_row(np, cr, y, "Enlace", v, th);
        y += NP_ROW_H;
    }
    if (np->d.mtu > 0) {
        snprintf(v, sizeof(v), "%d bytes", np->d.mtu);
        np_row(np, cr, y, "MTU", v, th);
        y += NP_ROW_H;
    }

    /* Wi-Fi radio toggle (only when nmcli reports a radio) */
    if (np->d.has_wifi_radio) {
        double bx = NP_PAD, bw = w - 2 * NP_PAD, bh = 34.0;
        if (np->hover == NP_HOVER_WIFI)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.35);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, 1.0);
        np_round(cr, bx, y + 4, bw, bh, 8);
        cairo_fill(cr);
        pango_layout_set_text(np->l_label, "Wi-Fi", -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
        cairo_move_to(cr, bx + 12, y + 4 + (bh - 16) / 2);
        pango_cairo_show_layout(cr, np->l_label);
        snprintf(v, sizeof(v), "%s", np->d.wifi_radio ? "Activado" : "Desactivado");
        pango_layout_set_text(np->l_value, v, -1);
        int vw = 0, vh = 0;
        pango_layout_get_pixel_size(np->l_value, &vw, &vh);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        cairo_move_to(cr, bx + bw - 12 - vw, y + 4 + (bh - vh) / 2);
        pango_cairo_show_layout(cr, np->l_value);
        np->wifi_row_y = y + 4;
    } else {
        np->wifi_row_y = -1;
    }
}

static void np_draw_wifi(struct network_panel *np, cairo_t *cr, const struct theme_config *th, int w)
{
    double top = np_content_top();
    np->disc_y = 0;

    if (!np->has_nmcli) {
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.8);
        pango_layout_set_text(np->l_value, "NetworkManager (nmcli) no est\xC3\xA1 disponible", -1);
        cairo_move_to(cr, NP_PAD, top + 4);
        pango_cairo_show_layout(cr, np->l_value);
        return;
    }

    if (np->n_aps == 0) {
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.7);
        pango_layout_set_text(np->l_value, "Sin redes Wi-Fi", -1);
        cairo_move_to(cr, NP_PAD, top + 4);
        pango_cairo_show_layout(cr, np->l_value);
    }

    int vis = np->n_aps < NP_AP_VISIBLE ? np->n_aps : NP_AP_VISIBLE;
    pango_layout_set_width(np->l_label, (int)((w - 2 * NP_PAD - 90) * PANGO_SCALE));
    pango_layout_set_ellipsize(np->l_label, PANGO_ELLIPSIZE_END);
    for (int i = 0; i < vis; i++) {
        int idx = np->scroll + i;
        if (idx >= np->n_aps) break;
        struct np_ap *a = &np->aps[idx];
        double ry = top + i * NP_AP_ROW_H;
        bool sel = (idx == np->sel);
        bool hov = (np->hover == NP_HOVER_AP_BASE + i);
        np_round(cr, NP_PAD, ry, w - 2 * NP_PAD, NP_AP_ROW_H - 8, 10);
        if (a->in_use)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.30);
        else if (sel)
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, 1.0);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, hov ? 0.95 : 0.75);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.98);
        pango_layout_set_text(np->l_label, a->ssid, -1);
        cairo_move_to(cr, NP_PAD + 12, ry + 4);
        pango_cairo_show_layout(cr, np->l_label);
        if (a->in_use) {
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.95);
            pango_layout_set_text(np->l_value, "Conectada", -1);
            cairo_move_to(cr, NP_PAD + 12, ry + 21);
            pango_cairo_show_layout(cr, np->l_value);
        }

        char sb[16];
        snprintf(sb, sizeof(sb), "%d%%", a->signal);
        pango_layout_set_text(np->l_value, sb, -1);
        int sw = 0, sh = 0;
        pango_layout_get_pixel_size(np->l_value, &sw, &sh);
        double rx = w - NP_PAD - 12 - sw;
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.9);
        cairo_move_to(cr, rx, ry + 10);
        pango_cairo_show_layout(cr, np->l_value);
        if (a->secure)
            np_lock_glyph(cr, rx - 18, ry + 9, 12, th->on_surface_variant);
    }
    pango_layout_set_width(np->l_label, -1);
    pango_layout_set_ellipsize(np->l_label, PANGO_ELLIPSIZE_NONE);

    double y = top + NP_AP_VISIBLE * NP_AP_ROW_H;

    if (np_ap_in_use(np)) {
        np->disc_y = y + 4;
        double bw = w - 2 * NP_PAD, bh = NP_BTN_H;
        if (np->hover == NP_HOVER_DISCONNECT)
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.35);
        else
            cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                                  th->surface_variant.b, 1.0);
        np_round(cr, NP_PAD, np->disc_y, bw, bh, 9);
        cairo_fill(cr);
        pango_layout_set_text(np->l_label, "Desconectar", -1);
        int tw2 = 0, th2 = 0;
        pango_layout_get_pixel_size(np->l_label, &tw2, &th2);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.95);
        cairo_move_to(cr, NP_PAD + (bw - tw2) / 2, np->disc_y + (bh - th2) / 2);
        pango_cairo_show_layout(cr, np->l_label);
        y = np->disc_y + bh + 6;
    }

    if (np->pw_prompt) {
        double pw_y = y;
        np_round(cr, NP_PAD, pw_y, w - 2 * NP_PAD, NP_PW_H, 9);
        cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g,
                              th->surface_variant.b, 1.0);
        cairo_fill(cr);
        char dots[80];
        int dn = np->pw_len;
        if (dn > 24) dn = 24;
        for (int i = 0; i < dn; i++) dots[i] = '*';
        dots[dn] = '\0';
        char buf[96];
        snprintf(buf, sizeof(buf), "Clave: %s", dots);
        pango_layout_set_text(np->l_value, buf, -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.98);
        cairo_move_to(cr, NP_PAD + 12, pw_y + 8);
        pango_cairo_show_layout(cr, np->l_value);
        y += NP_PW_H + 4;
        cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                              th->on_surface_variant.b, 0.75);
        pango_layout_set_text(np->l_label, "Escribe la clave y pulsa Enter (Esc cancela)", -1);
        cairo_move_to(cr, NP_PAD, y);
        pango_cairo_show_layout(cr, np->l_label);
        y += 18;
    }

    if (np->msg[0]) {
        cairo_set_source_rgba(cr,
                              np->msg_kind == 2 ? th->error.r : th->on_surface_variant.r,
                              np->msg_kind == 2 ? th->error.g : th->on_surface_variant.g,
                              np->msg_kind == 2 ? th->error.b : th->on_surface_variant.b, 0.95);
        pango_layout_set_text(np->l_value, np->msg, -1);
        cairo_move_to(cr, NP_PAD, y);
        pango_cairo_show_layout(cr, np->l_value);
    }
}

static void np_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct network_panel *np = p->userdata;
    (void)h;
    struct config def;
    config_defaults(&def);
    const struct theme_config *th = np->cfg ? &np->cfg->theme : &def.theme;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!np->l_title) {
        np->l_title = pango_cairo_create_layout(cr);
        np->l_label = pango_cairo_create_layout(cr);
        np->l_value = pango_cairo_create_layout(cr);
    }
    const char *family = (np->cfg && np->cfg->font.family[0]) ? np->cfg->font.family : "sans-serif";
    double fsize = (np->cfg && np->cfg->font.size > 0) ? np->cfg->font.size : 13.0;
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)((fsize + 3) * PANGO_SCALE));
    pango_font_description_set_weight(fd, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(np->l_title, fd);
    pango_font_description_set_size(fd, (int)((fsize - 1) * PANGO_SCALE));
    pango_layout_set_font_description(np->l_label, fd);
    pango_font_description_set_weight(fd, PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(np->l_value, fd);
    pango_font_description_free(fd);

    np_draw_tabs(np, cr, th, w);

    if (np->view == NP_VIEW_WIFI && np->d.has_wifi_radio)
        np_draw_wifi(np, cr, th, w);
    else
        np_draw_details(np, cr, th, w);
}

/* ------------------------------------------------------------ input / lifecycle */

static void np_sel_ensure_visible(struct network_panel *np)
{
    if (np->sel < np->scroll) np->scroll = np->sel;
    if (np->sel >= np->scroll + NP_AP_VISIBLE)
        np->scroll = np->sel - NP_AP_VISIBLE + 1;
    if (np->scroll < 0) np->scroll = 0;
}

static bool np_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)mods;
    struct network_panel *np = p->userdata;

    if (keysym == 0xff1b && !np->pw_prompt) { /* Escape */
        panel_hide(p);
        return true;
    }

    if (np->pw_prompt) {
        if (keysym == 0xff0d || keysym == 0xff8d) { /* Return / KP_Enter */
            if (np->pw_ap >= 0 && np->pw_ap < np->n_aps)
                np_wifi_connect(np, &np->aps[np->pw_ap], np->pw);
            else {
                np->pw_prompt = false;
                np->pw[0] = '\0';
                np->pw_len = 0;
            }
        } else if (keysym == 0xff1b) { /* Escape */
            np->pw_prompt = false;
            np->pw[0] = '\0';
            np->pw_len = 0;
        } else if (keysym == 0xff08) { /* BackSpace */
            if (np->pw_len > 0) np->pw[--np->pw_len] = '\0';
        } else if (keysym >= 0x20 && keysym <= 0x7e) {
            if (np->pw_len < (int)sizeof(np->pw) - 1) {
                np->pw[np->pw_len++] = (char)keysym;
                np->pw[np->pw_len] = '\0';
            }
        }
        panel_mark_dirty(p);
        return true;
    }

    if (np->view == NP_VIEW_WIFI && np->d.has_wifi_radio) {
        if (keysym == 0xff52 && np->n_aps) { /* Up */
            if (np->sel > 0) np->sel--;
            np_sel_ensure_visible(np);
            panel_mark_dirty(p);
            return true;
        }
        if (keysym == 0xff54 && np->n_aps) { /* Down */
            if (np->sel + 1 < np->n_aps) np->sel++;
            np_sel_ensure_visible(np);
            panel_mark_dirty(p);
            return true;
        }
        if ((keysym == 0xff0d || keysym == 0xff8d) && np->n_aps) { /* Return */
            np_wifi_select(np, np->sel);
            panel_mark_dirty(p);
            return true;
        }
    }
    return true;
}

static bool np_click(struct panel *p, double x, double y)
{
    struct network_panel *np = p->userdata;
    int w = p->logical_w;

    /* tab bar */
    if (np->d.has_wifi_radio && y >= NP_PAD && y < NP_PAD + NP_TAB_H) {
        double tw = (w - 2 * NP_PAD - NP_TAB_GAP) / 2.0;
        for (int i = 0; i < 2; i++) {
            double bx = NP_PAD + i * (tw + NP_TAB_GAP);
            if (x >= bx && x < bx + tw) {
                np->view = i;
                np->pw_prompt = false;
                if (i == NP_VIEW_WIFI) np_wifi_scan(np);
                panel_mark_dirty(p);
                return true;
            }
        }
        return true;
    }

    if (np->view == NP_VIEW_WIFI && np->d.has_wifi_radio) {
        if (np->pw_prompt) return true; /* keyboard only while typing */
        double top = np_content_top();
        for (int i = 0; i < NP_AP_VISIBLE; i++) {
            double ry = top + i * NP_AP_ROW_H;
            if (y >= ry && y < ry + NP_AP_ROW_H - 8) {
                int idx = np->scroll + i;
                if (idx < np->n_aps) {
                    np_wifi_select(np, idx);
                    panel_mark_dirty(p);
                }
                return true;
            }
        }
        if (np->disc_y > 0 && y >= np->disc_y && y < np->disc_y + NP_BTN_H) {
            np_wifi_disconnect(np);
            panel_mark_dirty(p);
            return true;
        }
        return true;
    }

    /* details: Wi-Fi radio toggle */
    if (np->wifi_row_y >= 0 && y >= np->wifi_row_y && y <= np->wifi_row_y + 34.0) {
        if (np->d.has_wifi_radio && np->has_nmcli) {
            char cmd[700], q[600];
            np_shell_quote(np->nmcli, q, sizeof(q));
            snprintf(cmd, sizeof(cmd), "%s radio wifi %s", q, np->d.wifi_radio ? "off" : "on");
            np_spawn(cmd);
        }
        return true;
    }
    return true;
}

static void np_motion(struct panel *p, double x, double y)
{
    struct network_panel *np = p->userdata;
    int w = p->logical_w;
    int want = NP_HOVER_NONE;

    if (np->d.has_wifi_radio && y >= NP_PAD && y < NP_PAD + NP_TAB_H) {
        double tw = (w - 2 * NP_PAD - NP_TAB_GAP) / 2.0;
        for (int i = 0; i < 2; i++) {
            double bx = NP_PAD + i * (tw + NP_TAB_GAP);
            if (x >= bx && x < bx + tw) {
                want = (i == 0) ? NP_HOVER_TAB_DETAILS : NP_HOVER_TAB_WIFI;
                break;
            }
        }
    } else if (np->view == NP_VIEW_WIFI && np->d.has_wifi_radio) {
        double top = np_content_top();
        for (int i = 0; i < NP_AP_VISIBLE; i++) {
            double ry = top + i * NP_AP_ROW_H;
            if (y >= ry && y < ry + NP_AP_ROW_H - 8) {
                if (np->scroll + i < np->n_aps) want = NP_HOVER_AP_BASE + i;
                break;
            }
        }
        if (want == NP_HOVER_NONE && np->disc_y > 0 &&
            y >= np->disc_y && y < np->disc_y + NP_BTN_H)
            want = NP_HOVER_DISCONNECT;
    } else if (np->wifi_row_y >= 0 && y >= np->wifi_row_y && y <= np->wifi_row_y + 34.0) {
        want = NP_HOVER_WIFI;
    }

    if (want != np->hover) {
        np->hover = want;
        panel_mark_dirty(p);
    }
}

struct network_panel *network_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                           struct output *out)
{
    struct network_panel *np = calloc(1, sizeof(*np));
    if (!np) return NULL;
    panel_init(&np->panel, ctx);
    np->cfg = cfg ? *cfg : NULL;
    np->fallback_out = out;
    np->wifi_row_y = -1;
    np->hover = NP_HOVER_NONE;
    np->view = NP_VIEW_DETAILS;
    np->pw_ap = -1;
    panel_set_callbacks(&np->panel, np_draw, np_key, np_click, np_motion, np);
    log_info("network panel created");
    return np;
}

void network_panel_set_backends(struct network_panel *np, const struct backends_state *bk)
{
    if (np) np->bk = bk;
}

void network_panel_destroy(struct network_panel *np)
{
    if (!np) return;
    if (np->l_title) g_object_unref(np->l_title);
    if (np->l_label) g_object_unref(np->l_label);
    if (np->l_value) g_object_unref(np->l_value);
    panel_fini(&np->panel);
    free(np);
}

struct panel *network_panel_surface(struct network_panel *np)
{
    return np ? &np->panel : NULL;
}

static void np_show(struct network_panel *np)
{
    np_sel_ensure_visible(np);
    int h = np_height(np);
    struct output *out = np->panel.out ? np->panel.out : np->fallback_out;
    if (!panel_show_at(&np->panel, out, NP_W, h, PANEL_ANCHOR_TOP_RIGHT))
        log_warn("network panel: show failed");
}

void network_panel_toggle_proxy(void *userdata)
{
    struct network_panel *np = userdata;
    if (!np) return;
    if (panel_is_visible(&np->panel)) {
        panel_hide(&np->panel);
        return;
    }
    np_gather(np);
    np->hover = NP_HOVER_NONE;
    np->pw_prompt = false;
    np->pw[0] = '\0';
    np->pw_len = 0;
    if (np->view == NP_VIEW_WIFI && np->d.has_wifi_radio)
        np_wifi_scan(np);
    np_show(np);
}

void network_panel_show_wifi(struct network_panel *np)
{
    if (!np) return;
    np_gather(np);
    np->view = NP_VIEW_WIFI;
    np->hover = NP_HOVER_NONE;
    np->pw_prompt = false;
    np->msg[0] = '\0';
    np->msg_kind = 0;
    if (np->d.has_wifi_radio) np_wifi_scan(np);
    np_show(np);
}

void network_panel_show_details(struct network_panel *np)
{
    if (!np) return;
    np_gather(np);
    np->view = NP_VIEW_DETAILS;
    np->hover = NP_HOVER_NONE;
    np->pw_prompt = false;
    np->msg[0] = '\0';
    np->msg_kind = 0;
    if (panel_is_visible(&np->panel)) panel_resize(&np->panel, NP_W, np_height(np));
    else np_show(np);
}

void network_panel_scan(struct network_panel *np)
{
    if (!np) return;
    if (!np->d.has_wifi_radio) np_gather(np);
    if (np->d.has_wifi_radio) np_wifi_scan(np);
    if (panel_is_visible(&np->panel)) panel_mark_dirty(&np->panel);
}

int network_panel_connect_index(struct network_panel *np, int idx, const char *password)
{
    if (!np || idx < 0 || idx >= np->n_aps) return -1;
    if (password && password[0]) np_wifi_connect(np, &np->aps[idx], password);
    else np_wifi_select(np, idx);
    if (panel_is_visible(&np->panel)) panel_mark_dirty(&np->panel);
    return 0;
}

int network_panel_disconnect_current(struct network_panel *np)
{
    if (!np || !np_ap_in_use(np)) return -1;
    np_wifi_disconnect(np);
    if (panel_is_visible(&np->panel)) panel_mark_dirty(&np->panel);
    return 0;
}

void network_panel_status(struct network_panel *np, char *out, size_t n)
{
    if (!out || !n) return;
    if (!np) { snprintf(out, n, "unavailable"); return; }
    const char *conn = "\xE2\x80\x94", *sel = "\xE2\x80\x94";
    for (int i = 0; i < np->n_aps; i++) {
        if (np->aps[i].in_use) conn = np->aps[i].ssid;
        if (i == np->sel) sel = np->aps[i].ssid;
    }
    snprintf(out, n,
             "view=%s radio=%s iface=%s aps=%d connected=%s sel=%s connecting=%s pw_prompt=%d msg=%s",
             np->view == NP_VIEW_WIFI ? "wifi" : "details",
             np->d.has_wifi_radio ? (np->d.wifi_radio ? "on" : "off") : "none",
             np->d.wifi_iface[0] ? np->d.wifi_iface : (np->d.iface[0] ? np->d.iface : "\xE2\x80\x94"),
             np->n_aps, conn, sel, np->connecting ? "yes" : "no", np->pw_prompt ? 1 : 0,
             np->msg[0] ? np->msg : "\xE2\x80\x94");
}
