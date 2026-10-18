static struct sysinfo_state sys;
static struct backends_state bk;
static int last_cpu=-2,last_ram=-2,last_bat=-2,last_chg=-1;
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <errno.h>
#include <stddef.h>
#include <locale.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include "wayland-client.h"
#include "core/wayland.h"
#include "core/output.h"
#include "core/registry.h"
#include "core/seat.h"
#include "core/surface_mgr.h"
#include "util/log.h"
#include "config/config.h"
#include "config/parser.h"
#include "config/watcher.h"
#include "bar/workspaces.h"
#include "bar/sysinfo.h"
#include "bar/backends.h"
#include "launcher/launcher.h"
#include "launcher/panel.h"
#include "core/panel.h"
#include "compositors/niri_ipc.h"
#include "control/calendar_panel.h"
#include "dock/dock.h"
#include "widgets/desktop_widgets.h"
#include "switcher/switcher.h"
#include "control/control_center.h"
#include "control/network_panel.h"
#include "control/bluetooth_panel.h"
#include "osd/osd.h"
#include "notify/notify_daemon.h"
#include "notify/toast.h"
#include "media/mpris.h"
#include "systray/systray.h"
#include "session/lock_screen.h"
#include "session/session_actions.h"
#include "session/idle.h"
#include "wallpaper/wallpaper.h"
#include "hotkeys/hotkeys.h"
#include "ipc/ipc.h"
#include "clipboard/clipboard.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"

static volatile sig_atomic_t g_shutdown = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

static struct wallpaper *g_wp = NULL;
static struct lock_screen *g_lock = NULL;
/* Monotonic timerfd driving interval-based wallpaper rotation. */
static int g_wpfd = -1;

static void arm_wallpaper_timer(int interval_min)
{
    if (g_wpfd < 0) return;
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    if (interval_min > 0) {
        its.it_value.tv_sec = (time_t)interval_min * 60;
        its.it_interval.tv_sec = (time_t)interval_min * 60;
    }
    timerfd_settime(g_wpfd, 0, &its, NULL);
}
static struct idle_manager *g_idle = NULL;
static struct launcher *g_launcher = NULL;
static struct dock *g_dock = NULL;
static struct desktop_widgets *g_dw = NULL;
static struct switcher *g_switcher = NULL;
static void on_config_changed(void *userdata, const struct config *cfg, bool applied)
{
    struct surface_mgr *mgr = userdata;
    if (!applied) {
        log_warn("config not applied, keeping last valid");
        return;
    }
    log_info("config applied: bar.height=%d font=\"%s\" %d", cfg->bar.height, cfg->font.family, cfg->font.size);
    if (mgr && mgr->ctx->compositor) surface_mgr_apply_config(mgr, cfg);
    if (mgr && mgr->ctx->compositor) surface_mgr_mark_dirty_all(mgr);
    if (g_wp) {
        wallpaper_set_path(g_wp, cfg->wallpaper.path);
        wallpaper_set_dir(g_wp, cfg->wallpaper.dir);
        if (!cfg->wallpaper.path[0]) wallpaper_autodetect(g_wp);
        wallpaper_set_fill(g_wp, cfg->wallpaper.fill);
        wallpaper_set_transition(g_wp, cfg->wallpaper.transition);
        wallpaper_set_fallback(g_wp, cfg->theme.background.r,
                               cfg->theme.background.g, cfg->theme.background.b);
        wallpaper_set_enabled(g_wp, cfg->wallpaper.enabled);
    }
    arm_wallpaper_timer(cfg->wallpaper.interval);
    if (g_idle) idle_manager_apply(g_idle, cfg);
    if (g_lock) lock_screen_set_config(g_lock, cfg);
    if (g_launcher) launcher_set_config(g_launcher, cfg);
    if (g_dock) dock_update_config(g_dock, cfg);
    if (g_dw) desktop_widgets_update_config(g_dw, cfg);
    if (g_switcher) switcher_set_config(g_switcher, cfg);
}

static void on_workspaces_changed(void *userdata)
{
    surface_mgr_request_redraw(userdata);
}

/* Fire at the start of every minute so the clock flips on time. */
static void arm_clock(int tfd)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct itimerspec its = {
        .it_value = { .tv_sec = now.tv_sec + 1, .tv_nsec = 0 },
        .it_interval = { .tv_sec = 1, .tv_nsec = 0 },
    };
    timerfd_settime(tfd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &its, NULL);
}

static void report_display_error(struct wl_display *display)
{
    int err = wl_display_get_error(display);
    if (err == EPROTO) {
        const struct wl_interface *iface = NULL;
        uint32_t id = 0;
        uint32_t code = wl_display_get_protocol_error(display, &iface, &id);
        log_err("wayland protocol error %u on %s#%u", code, iface ? iface->name : "?", id);
    } else {
        log_err("wayland connection lost: %s", strerror(err));
    }
}

static struct osd *osd_global = NULL;
static struct toast_panel *toast_global = NULL;
static struct launcher_panel *g_lpanel = NULL;
static struct niri_ipc *g_niri = NULL;
static struct network_panel *g_npanel = NULL;
static struct bluetooth_panel *g_btpanel = NULL;

/* Control-center shortcuts that act on the shell itself (see cc_shortcuts). */
static void cc_shortcut_handler(void *ud, const char *id)
{
    (void)ud;
    if (!id) return;
    if (strcmp(id, "launcher") == 0) {
        if (g_lpanel) launcher_panel_toggle_proxy(g_lpanel);
    } else if (strcmp(id, "screen_off") == 0) {
        if (g_niri) niri_ipc_power_off_monitors(g_niri);
    } else if (strcmp(id, "network") == 0) {
        if (g_npanel) network_panel_toggle_proxy(g_npanel);
    } else if (strcmp(id, "bluetooth") == 0) {
        if (g_btpanel) bluetooth_panel_toggle_proxy(g_btpanel);
    }
}
static void on_idle_screen_off(void *userdata)
{
    struct niri_ipc *niri = userdata;
    if (niri) niri_ipc_power_off_monitors(niri);
}static bool panel_keys_proxy(uint32_t keysym, uint32_t mods, void *userdata)
{
    (void)mods;
    struct surface_mgr *mgr = (struct surface_mgr *)userdata;
    if (keysym == XKB_KEY_XF86AudioRaiseVolume) {
        if (osd_global) { osd_show(osd_global, OSD_VOLUME, 80); osd_tick(osd_global); }
        return true;
    }
    if (keysym == XKB_KEY_XF86AudioLowerVolume) {
        if (osd_global) { osd_show(osd_global, OSD_VOLUME, 40); osd_tick(osd_global); }
        return true;
    }
    if (keysym == XKB_KEY_XF86AudioMute) {
        if (osd_global) { osd_show(osd_global, OSD_VOLUME, 0); osd_tick(osd_global); }
        return true;
    }
    if (keysym == XKB_KEY_XF86MonBrightnessUp) {
        if (osd_global) { osd_show(osd_global, OSD_BRIGHTNESS, 90); osd_tick(osd_global); }
        return true;
    }
    if (keysym == XKB_KEY_XF86MonBrightnessDown) {
        if (osd_global) { osd_show(osd_global, OSD_BRIGHTNESS, 30); osd_tick(osd_global); }
        return true;
    }
    return surface_mgr_handle_key(mgr, keysym, mods);
}

/* ---- IPC command dispatch ------------------------------------------------ */

struct ipc_ctx {
    struct surface_mgr *mgr;
    struct launcher_panel *lpanel;
    struct calendar_panel *calpanel;
    struct control_center *cc;
    struct network_panel *npanel;
    struct bluetooth_panel *btpanel;
    struct wallpaper *wp;
    struct lock_screen *lock;
    struct session_actions *sa;
    struct osd *osd;
    struct config_watcher *cw;
    struct clipboard *clip;
    struct output *out;
    struct switcher *switcher;
};

static char *ipc_strdup(const char *s) { return s ? strdup(s) : NULL; }

static char *ipc_handle(const char *cmd, void *ud)
{
    struct ipc_ctx *c = ud;
    if (!cmd || !*cmd) return ipc_strdup("ok");

    if (strcmp(cmd, "ping") == 0) return ipc_strdup("pong");
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        if (c->mgr) c->mgr->shutdown = true;
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "reload") == 0) {
        if (c->cw) config_watcher_reload_now(c->cw);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "launcher") == 0 || strcmp(cmd, "launcher toggle") == 0) {
        if (c->lpanel) launcher_panel_toggle(c->lpanel, c->out);
        return ipc_strdup("ok");
    }
    if (strncmp(cmd, "launcher query ", 15) == 0) {
        if (c->lpanel) launcher_panel_show_query(c->lpanel, c->out, cmd + 15);
        return ipc_strdup("ok");
    }
    if (strncmp(cmd, "launcher activate ", 18) == 0) {
        int idx = atoi(cmd + 18);
        if (c->lpanel) launcher_panel_activate_index(c->lpanel, idx);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "launcher hide") == 0) {
        if (c->lpanel) launcher_panel_hide(c->lpanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "calendar") == 0 || strcmp(cmd, "calendar toggle") == 0) {
        if (c->calpanel) calendar_panel_toggle_proxy(c->calpanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "cc") == 0 || strcmp(cmd, "cc toggle") == 0 ||
        strcmp(cmd, "control-center") == 0 || strcmp(cmd, "control-center toggle") == 0) {
        if (c->cc) control_center_toggle_proxy(c->cc);
        return ipc_strdup("ok");
    }
    if (strncmp(cmd, "cc tab ", 7) == 0) {
        if (c->cc) control_center_set_tab(c->cc, atoi(cmd + 7));
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "network") == 0 || strcmp(cmd, "network toggle") == 0) {
        if (c->npanel) network_panel_toggle_proxy(c->npanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "network wifi") == 0) {
        if (c->npanel) network_panel_show_wifi(c->npanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "network details") == 0) {
        if (c->npanel) network_panel_show_details(c->npanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "network scan") == 0) {
        if (c->npanel) network_panel_scan(c->npanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "network status") == 0) {
        char buf[512];
        network_panel_status(c->npanel, buf, sizeof(buf));
        return ipc_strdup(buf);
    }
    if (strncmp(cmd, "network connect ", 16) == 0) {
        if (!c->npanel) return ipc_strdup("error: no network panel");
        int idx = -1;
        char pw[128] = "";
        sscanf(cmd + 16, "%d %127[^\n]", &idx, pw);
        int rc = network_panel_connect_index(c->npanel, idx, pw[0] ? pw : NULL);
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strcmp(cmd, "network disconnect") == 0) {
        if (!c->npanel) return ipc_strdup("error: no network panel");
        int rc = network_panel_disconnect_current(c->npanel);
        return ipc_strdup(rc == 0 ? "ok" : "error: not connected");
    }
    if (strcmp(cmd, "bluetooth") == 0 || strcmp(cmd, "bluetooth toggle") == 0) {
        if (c->btpanel) bluetooth_panel_toggle_proxy(c->btpanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "bluetooth open") == 0) {
        if (c->btpanel) bluetooth_panel_open(c->btpanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "bluetooth scan") == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        bluetooth_panel_scan(c->btpanel);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "bluetooth status") == 0) {
        char buf[512];
        bluetooth_panel_status(c->btpanel, buf, sizeof(buf));
        return ipc_strdup(buf);
    }
    if (strncmp(cmd, "bluetooth action ", 17) == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        int rc = bluetooth_panel_primary_action(c->btpanel, atoi(cmd + 17));
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strncmp(cmd, "bluetooth connect ", 18) == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        int rc = bluetooth_panel_primary_action(c->btpanel, atoi(cmd + 18));
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strncmp(cmd, "bluetooth pair ", 15) == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        int rc = bluetooth_panel_pair(c->btpanel, atoi(cmd + 15));
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strncmp(cmd, "bluetooth disconnect ", 21) == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        int rc = bluetooth_panel_disconnect(c->btpanel, atoi(cmd + 21));
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strncmp(cmd, "bluetooth forget ", 17) == 0) {
        if (!c->btpanel) return ipc_strdup("error: no bluetooth panel");
        int rc = bluetooth_panel_forget(c->btpanel, atoi(cmd + 17));
        return ipc_strdup(rc == 0 ? "ok" : "error: invalid index");
    }
    if (strcmp(cmd, "switcher") == 0 || strcmp(cmd, "switcher toggle") == 0) {
        if (c->switcher) switcher_toggle(c->switcher, c->out);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "switcher next") == 0) { if (c->switcher) switcher_next(c->switcher); return ipc_strdup("ok"); }
    if (strcmp(cmd, "switcher prev") == 0) { if (c->switcher) switcher_prev(c->switcher); return ipc_strdup("ok"); }
    if (strcmp(cmd, "switcher select") == 0) { if (c->switcher) switcher_select(c->switcher); return ipc_strdup("ok"); }
    if (strcmp(cmd, "switcher hide") == 0) { if (c->switcher) switcher_hide(c->switcher); return ipc_strdup("ok"); }
    if (strcmp(cmd, "switcher status") == 0) return ipc_strdup(c->switcher && switcher_visible(c->switcher) ? "open" : "closed");
    if (strncmp(cmd, "theme ", 6) == 0) {
        const char *rest = cmd + 6;
        bool is_dark;
        if (strncmp(rest, "dark", 4) == 0) is_dark = true;
        else if (strncmp(rest, "night", 5) == 0) is_dark = false;
        else return ipc_strdup("error: expected 'theme dark|night [on|off]'");
        const char *arg = rest + (is_dark ? 4 : 5);
        while (*arg == ' ' || *arg == '\t') arg++;
        bool val;
        if (*arg) {
            val = (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0 || strcmp(arg, "true") == 0);
        } else {
            const struct config *cur = c->cw ? config_watcher_get_last(c->cw) : NULL;
            val = is_dark ? !(cur && cur->theme.dark) : !(cur && cur->theme.night);
        }
        if (is_dark) config_write_theme_dark(val); else config_write_theme_night(val);
        if (c->cw) config_watcher_reload_now(c->cw);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "wallpaper next") == 0) { if (c->wp) wallpaper_next(c->wp); return ipc_strdup("ok"); }
    if (strcmp(cmd, "wallpaper prev") == 0) { if (c->wp) wallpaper_prev(c->wp); return ipc_strdup("ok"); }
    if (strcmp(cmd, "wallpaper random") == 0) { if (c->wp) wallpaper_random(c->wp); return ipc_strdup("ok"); }
    if (strncmp(cmd, "wallpaper set ", 14) == 0) {
        const char *p = cmd + 14;
        if (c->wp) wallpaper_set_path(c->wp, p);
        config_write_wallpaper_path(p);
        return ipc_strdup("ok");
    }
    if (strncmp(cmd, "wallpaper dir ", 14) == 0) {
        const char *p = cmd + 14;
        if (c->wp) { wallpaper_set_dir(c->wp, p); if (!p[0]) wallpaper_autodetect(c->wp); }
        config_write_wallpaper_dir(p);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "wallpaper") == 0) {
        const char *p = c->wp ? wallpaper_get_path(c->wp) : "";
        return ipc_strdup(p && p[0] ? p : "(none)");
    }
    if (strcmp(cmd, "lock") == 0) {
        if (!c->lock) return ipc_strdup("no lock");
        if (!lock_screen_enabled(c->lock)) return ipc_strdup("disabled");
        lock_screen_lock(c->lock);
        return ipc_strdup(lock_screen_is_locked(c->lock) ? "ok" : "unavailable");
    }
    if (strcmp(cmd, "lock status") == 0)
        return ipc_strdup(lock_screen_enabled(c->lock) ? "on" : "off");
    if (strcmp(cmd, "lock enable") == 0 || strcmp(cmd, "lock true") == 0 ||
        strcmp(cmd, "lock enabled on") == 0) {
        config_write_lock_enabled(true);
        if (c->cw) config_watcher_reload_now(c->cw);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "lock disable") == 0 || strcmp(cmd, "lock false") == 0 ||
        strcmp(cmd, "lock enabled off") == 0) {
        config_write_lock_enabled(false);
        if (c->cw) config_watcher_reload_now(c->cw);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "lock toggle") == 0) {
        bool on = !lock_screen_enabled(c->lock);
        config_write_lock_enabled(on);
        if (c->cw) config_watcher_reload_now(c->cw);
        return ipc_strdup(on ? "on" : "off");
    }
    if (strcmp(cmd, "unlock") == 0) { if (c->lock) lock_screen_unlock(c->lock); return ipc_strdup("ok"); }
    if (strncmp(cmd, "session ", 8) == 0) {
        const char *a = cmd + 8;
        if (!c->sa) return ipc_strdup("no session");
        if (strcmp(a, "poweroff") == 0) session_actions_poweroff(c->sa);
        else if (strcmp(a, "reboot") == 0) session_actions_reboot(c->sa);
        else if (strcmp(a, "suspend") == 0) session_actions_suspend(c->sa);
        else if (strcmp(a, "logout") == 0) session_actions_logout(c->sa);
        else return ipc_strdup("unknown session action");
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "clipboard") == 0) {
        if (!c->clip) return ipc_strdup("no clipboard");
        char *r = malloc(32);
        if (r) snprintf(r, 32, "%d", c->clip->count);
        return r;
    }
    if (strcmp(cmd, "clipboard last") == 0) {
        if (!c->clip || c->clip->count == 0) return ipc_strdup("(empty)");
        return ipc_strdup(c->clip->items[c->clip->count - 1].text);
    }
    if (strncmp(cmd, "clipboard set ", 14) == 0) {
        if (!c->clip) return ipc_strdup("no clipboard");
        clipboard_set_text(c->clip, cmd + 14);
        return ipc_strdup("ok");
    }
    if (strncmp(cmd, "clipboard paste ", 16) == 0) {
        if (!c->clip) return ipc_strdup("no clipboard");
        int idx = atoi(cmd + 16);
        if (idx < 0 || idx >= c->clip->count) return ipc_strdup("out of range");
        clipboard_set_text(c->clip, c->clip->items[idx].text);
        return ipc_strdup("ok");
    }
    if (strcmp(cmd, "volume up") == 0) { backends_volume_step(&bk, 5); if (c->osd) { osd_show(c->osd, OSD_VOLUME, bk.vol_pct >= 0 ? bk.vol_pct : 80); osd_tick(c->osd); } return ipc_strdup("ok"); }
    if (strcmp(cmd, "volume down") == 0) { backends_volume_step(&bk, -5); if (c->osd) { osd_show(c->osd, OSD_VOLUME, bk.vol_pct >= 0 ? bk.vol_pct : 40); osd_tick(c->osd); } return ipc_strdup("ok"); }
    if (strcmp(cmd, "volume mute") == 0) { backends_volume_toggle_mute(&bk); if (c->osd) { osd_show(c->osd, OSD_VOLUME, bk.vol_muted ? 0 : (bk.vol_pct >= 0 ? bk.vol_pct : 50)); osd_tick(c->osd); } return ipc_strdup("ok"); }
    if (strcmp(cmd, "brightness up") == 0) { backends_brightness_step(&bk, 5); if (c->osd) { osd_show(c->osd, OSD_BRIGHTNESS, bk.br_pct >= 0 ? bk.br_pct : 90); osd_tick(c->osd); } return ipc_strdup("ok"); }
    if (strcmp(cmd, "brightness down") == 0) { backends_brightness_step(&bk, -5); if (c->osd) { osd_show(c->osd, OSD_BRIGHTNESS, bk.br_pct >= 0 ? bk.br_pct : 30); osd_tick(c->osd); } return ipc_strdup("ok"); }

    char *unknown = malloc(strlen(cmd) + 32);
    if (unknown) snprintf(unknown, strlen(cmd) + 32, "unknown command: %s", cmd);
    return unknown;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && argv[1][0] != '-') {
        /* Forward everything after the program name to a running instance. */
        char cmd[1024] = {0};
        size_t off = 0;
        for (int i = 1; i < argc && off < sizeof(cmd) - 1; i++) {
            int n = snprintf(cmd + off, sizeof(cmd) - off, "%s%s", i > 1 ? " " : "", argv[i]);
            if (n < 0) break;
            off += (size_t)n;
        }
        char *reply = ipc_send_command(cmd);
        if (reply) {
            printf("%s\n", reply);
            free(reply);
            return 0;
        }
        fprintf(stderr, "emancipation-shell: not running (is the shell up?)\n");
        return 1;
    }
    setlocale(LC_ALL, "");
    log_set_level(getenv("EMANCIPATION_DEBUG") ? LOG_DEBUG : LOG_INFO);

    struct wl_display *display = wl_display_connect(NULL); /* honours WAYLAND_DISPLAY */
    if (!display) {
        log_err("failed to connect to Wayland display");
        return 1;
    }
    log_info("connected to Wayland display");

    struct wayland_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.display = display;

    struct wl_list outputs;
    wl_list_init(&outputs);

    struct surface_mgr mgr;
    surface_mgr_init(&mgr, &ctx, &outputs);

    /* must exist before the registry so the first workspace burst is caught */
    struct workspace_manager *wm = workspaces_create(&ctx);
    ctx.workspaces = wm;
    workspaces_set_changed_cb(wm, on_workspaces_changed, &mgr);

    /* config first: bar height/visibility are needed when surfaces are made */
    const char *cfg_path = config_default_path();
    struct config_watcher *cw = config_watcher_create(cfg_path, NULL, &mgr, on_config_changed);
    struct config cfg_def;
    config_defaults(&cfg_def);
    const struct config *cfg = &cfg_def;
    if (cw) {
        config_watcher_reload_now(cw);
        cfg = config_watcher_get_last(cw);
        log_info("config loaded: %s", cfg_path);
    } else {
        log_warn("config watcher could not be initialized, using defaults");
    }
    surface_mgr_set_sources(&mgr, cfg, wm);
    sysinfo_init(&sys);
    sysinfo_update(&sys);
    surface_mgr_set_sysinfo(&mgr, &sys);
    backends_init(&bk);
    surface_mgr_set_backends(&mgr, &bk);

    ctx.seat = seat_create(&ctx, &mgr);
    struct registry_state reg_state;
    registry_init(&reg_state, &ctx, &outputs, &mgr);
    registry_bind_globals(&reg_state);
    if (wl_display_roundtrip(display) < 0 || wl_display_roundtrip(display) < 0) {
        report_display_error(display);
        return 1;
    }

    int rc = 0;
    if (!ctx.compositor || !ctx.shm || !ctx.layer_shell) {
        log_err("compositor lacks required globals (wl_compositor=%d wl_shm=%d zwlr_layer_shell_v1=%d)",
                !!ctx.compositor, !!ctx.shm, !!ctx.layer_shell);
        rc = 1;
        goto cleanup;
    }
    if (!ctx.workspace_manager) log_warn("ext_workspace_manager_v1 unavailable: no workspace list");

    /* outputs announced before layer_shell were skipped by the registry */
    struct output *o;
    wl_list_for_each(o, &outputs, link) {
        surface_mgr_on_output_added(&mgr, o);
    }

    struct output *out_first = wl_list_empty(&outputs) ? NULL
        : wl_container_of(outputs.next, out_first, link);
    struct launcher *launcher = launcher_create(&ctx);
    g_launcher = launcher;
    launcher_set_config(launcher, cfg);
    surface_mgr_set_launcher(&mgr, launcher);
    struct launcher_panel *lpanel = launcher_panel_create(&ctx, launcher, &cfg, out_first);
    g_lpanel = lpanel;
    surface_mgr_add_panel(&mgr, launcher_panel_surface(lpanel));
    mgr.focus_output = out_first;
    /* keys go to whichever panel is open (launcher search, calendar nav) */
    if (ctx.seat) seat_set_key_handler(ctx.seat, panel_keys_proxy, &mgr);
    surface_mgr_set_launcher_toggle(&mgr, launcher_panel_toggle_proxy, lpanel);
    struct calendar_panel *calpanel = calendar_panel_create(&ctx, &cfg, out_first);
    surface_mgr_add_panel(&mgr, calendar_panel_surface(calpanel));
    surface_mgr_set_calendar_toggle(&mgr, calendar_panel_toggle_proxy, calpanel);

    struct dock *dock = NULL;
    struct control_center *cc = NULL;
    struct network_panel *npanel = NULL;
    struct bluetooth_panel *btpanel = NULL;
    struct osd *osd = NULL;
    struct wallpaper *wp = NULL;
    struct switcher *switcher = NULL;
    if (out_first) {
        dock = dock_create(&ctx, out_first, cfg);
        g_dock = dock;
        if (dock) {
            dock_set_launcher(dock, launcher);
            dock_set_launcher_toggle(dock, launcher_panel_toggle_proxy, lpanel);
            surface_mgr_add_panel(&mgr, dock_surface(dock));
        }
        struct desktop_widgets *dw = desktop_widgets_create(&ctx, out_first, cfg);
        g_dw = dw;
        if (dw) {
            desktop_widgets_set_sysinfo(dw, &sys);
            surface_mgr_add_panel(&mgr, desktop_widgets_surface(dw));
        }
        cc = control_center_create(&ctx, &cfg, out_first);
        surface_mgr_add_panel(&mgr, control_center_surface(cc));
        surface_mgr_set_control_center_toggle(&mgr, control_center_toggle_proxy, cc);
        npanel = network_panel_create(&ctx, &cfg, out_first);
        network_panel_set_backends(npanel, &bk);
        surface_mgr_add_panel(&mgr, network_panel_surface(npanel));
        surface_mgr_set_network_toggle(&mgr, network_panel_toggle_proxy, npanel);
        g_npanel = npanel;
        btpanel = bluetooth_panel_create(&ctx, &cfg, out_first);
        surface_mgr_add_panel(&mgr, bluetooth_panel_surface(btpanel));
        g_btpanel = btpanel;
        switcher = switcher_create(&ctx, &cfg, out_first);
        g_switcher = switcher;
        if (switcher) surface_mgr_add_panel(&mgr, switcher_surface(switcher));
        osd = osd_create(&ctx, out_first);
        osd_global = osd;
        surface_mgr_add_panel(&mgr, (struct panel*)osd); /* osd is panel-based */
        toast_global = toast_create(&ctx, out_first);
        if (toast_global) surface_mgr_add_panel(&mgr, (struct panel*)toast_global);
        wp = wallpaper_create(&ctx, out_first);
        g_wp = wp;
        if (wp) {
            wallpaper_set_path(wp, cfg->wallpaper.path);
            wallpaper_set_dir(wp, cfg->wallpaper.dir);
            if (!cfg->wallpaper.path[0]) wallpaper_autodetect(wp);
            wallpaper_set_fill(wp, cfg->wallpaper.fill);
            wallpaper_set_transition(wp, cfg->wallpaper.transition);
            wallpaper_set_fallback(wp, cfg->theme.background.r,
                                   cfg->theme.background.g, cfg->theme.background.b);
            wallpaper_set_enabled(wp, cfg->wallpaper.enabled);
        }
    }
    struct notify_daemon *nd = notify_daemon_create(&ctx);
    if (nd && toast_global) notify_daemon_set_toast(nd, toast_global);
    if (cc && nd) control_center_set_notifications(cc, notify_daemon_store(nd));
    struct mpris *mpris = mpris_create(&bk);
    struct lock_screen *lock = lock_screen_create(&ctx, &outputs);
    g_lock = lock;
    lock_screen_set_config(lock, cfg);
    struct session_actions *sa = session_actions_create();
    if (cc) control_center_set_actions(cc, sa, lock);
    struct hotkeys *hk = hotkeys_create();
    struct clipboard *clip = clipboard_create(100);
    if (clip) clipboard_connect(clip, &ctx);
    launcher_set_clipboard(launcher, clip);
    launcher_set_wallpaper(launcher, wp);
    struct ipc_ctx ictx = {
        .mgr = &mgr, .lpanel = lpanel, .calpanel = calpanel, .cc = cc, .npanel = npanel,
        .btpanel = btpanel,
        .wp = wp, .lock = lock, .sa = sa, .osd = osd, .cw = cw, .out = out_first,
        .clip = clip, .switcher = switcher,
    };
    struct ipc_server *ipc = ipc_create(&ctx, ipc_handle, &ictx);
    struct systray *systray = systray_create(&ctx);
    if (systray) surface_mgr_set_systray(&mgr, systray);

    struct sigaction sact;
    memset(&sact, 0, sizeof(sact));
    sact.sa_handler = handle_signal;
    sigaction(SIGINT, &sact, NULL);
    sigaction(SIGTERM, &sact, NULL);
    signal(SIGPIPE, SIG_IGN);

    int tfd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd >= 0) arm_clock(tfd);

    /* Dedicated monotonic timer for wallpaper rotation, independent of wall
     * clock jumps (the CLOCK_REALTIME tick can stall across NTP steps). */
    g_wpfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g_wpfd >= 0) arm_wallpaper_timer(cfg->wallpaper.interval);

    enum { FD_WL, FD_CLOCK, FD_WPROT, FD_CFG, FD_NIRI, FD_SYSTRAY, FD_IPC, FD_DBUS, FD_MPRIS, FD_COUNT };
    struct pollfd fds[FD_COUNT] = {
        [FD_WL] = { .fd = wl_display_get_fd(display), .events = POLLIN },
        [FD_CLOCK] = { .fd = tfd, .events = POLLIN },
        [FD_WPROT] = { .fd = g_wpfd, .events = POLLIN },
        [FD_CFG] = { .fd = config_watcher_get_fd(cw), .events = POLLIN },
        [FD_NIRI] = { .fd = -1, .events = POLLIN },
        [FD_SYSTRAY] = { .fd = systray ? systray_fd(systray) : -1, .events = POLLIN },
        [FD_IPC] = { .fd = ipc ? ipc_fd(ipc) : -1, .events = POLLIN },
        [FD_DBUS] = { .fd = nd ? notify_daemon_fd(nd) : -1, .events = POLLIN },
        [FD_MPRIS] = { .fd = mpris ? mpris_fd(mpris) : -1, .events = POLLIN },
    };

    /* niri IPC feeds the taskbar; it is optional (other compositors) */
    struct niri_ipc *niri = niri_ipc_create(NULL, NULL);
    if (niri) {
        surface_mgr_set_niri(&mgr, niri);
        g_niri = niri;
        if (cc) control_center_set_niri(cc, niri);
        if (dock) dock_set_niri(dock, niri);
        if (switcher) switcher_set_niri(switcher, niri);
        log_info("niri windows: %d", niri_ipc_window_count(niri));
    }
    if (cc) control_center_set_shortcut_handler(cc, cc_shortcut_handler, NULL);

    g_idle = idle_manager_create(&ctx, lock);
    if (g_idle) {
        if (niri) idle_manager_set_screen_off(g_idle, on_idle_screen_off, niri);
        idle_manager_apply(g_idle, cfg);
    }

    log_info("emancipation-shell running");
    while (!g_shutdown && !mgr.shutdown) {
        while (wl_display_prepare_read(display) != 0) {
            if (wl_display_dispatch_pending(display) < 0) goto wl_error;
        }
        if (wl_display_flush(display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display);
            goto wl_error;
        }
        if (poll(fds, FD_COUNT, -1) < 0) {
            wl_display_cancel_read(display);
            if (errno == EINTR) continue;
            log_err("poll failed: %s", strerror(errno));
            rc = 1;
            break;
        }
        if (fds[FD_WL].revents & (POLLERR | POLLHUP)) {
            wl_display_cancel_read(display);
            goto wl_error;
        }
        if (fds[FD_WL].revents & POLLIN) {
            if (wl_display_read_events(display) < 0) goto wl_error;
        } else {
            wl_display_cancel_read(display);
        }
        if (wl_display_dispatch_pending(display) < 0) goto wl_error;

        if (fds[FD_CLOCK].revents & POLLIN) {
            uint64_t exp;
            if (read(tfd, &exp, sizeof(exp)) < 0 && errno == ECANCELED) {
                arm_clock(tfd); /* wall clock jumped: realign */
            }
            bool chg = sysinfo_update(&sys);
            if (sys.cpu_pct != last_cpu || sys.ram_pct != last_ram || sys.bat_pct != last_bat || (last_chg != (sys.bat_charging?1:0)) || chg) {
                last_cpu=sys.cpu_pct; last_ram=sys.ram_pct; last_bat=sys.bat_pct; last_chg=sys.bat_charging?1:0;
                surface_mgr_mark_dirty_all(&mgr);
            }
            if (osd) osd_tick(osd);
            if (toast_global) toast_tick(toast_global);
            if (cc) control_center_tick(cc);
            if (npanel) network_panel_tick(npanel);
            if (btpanel) bluetooth_panel_tick(btpanel);
            if (g_dw) desktop_widgets_mark_dirty(g_dw);
            if (g_lock) lock_screen_refresh(g_lock);
            if (backends_update(&bk, cfg, clip ? clipboard_unpinned_count(clip) : 0))
                surface_mgr_mark_dirty_all(&mgr);
        }
        if (fds[FD_WPROT].revents & POLLIN) {
            uint64_t exp;
            if (read(g_wpfd, &exp, sizeof(exp)) > 0 &&
                wp && cfg && cfg->wallpaper.enabled && cfg->wallpaper.interval > 0) {
                if (cfg->wallpaper.randomize) wallpaper_random(wp);
                else wallpaper_next(wp);
            }
        }
        if (fds[FD_CFG].revents & POLLIN) {
            config_watcher_dispatch(cw);
        }
        if (niri) {
            int nfd = niri_ipc_fd(niri);
            if (nfd != fds[FD_NIRI].fd) fds[FD_NIRI].fd = nfd; /* (re)connect later */
            if (fds[FD_NIRI].fd >= 0 && (fds[FD_NIRI].revents & (POLLIN | POLLHUP))) {
                if (niri_ipc_dispatch(niri)) {
                    log_info("niri windows: %d", niri_ipc_window_count(niri));
                    surface_mgr_mark_dirty_all(&mgr);
                    if (dock) dock_mark_dirty(dock);
                    if (switcher) switcher_refresh(switcher);
                }
            }
        }
        if (systray && fds[FD_SYSTRAY].fd >= 0 &&
            (fds[FD_SYSTRAY].revents & (POLLIN | POLLHUP))) {
            if (systray_dispatch(systray)) surface_mgr_mark_dirty_all(&mgr);
        }
        if (ipc && fds[FD_IPC].fd >= 0 && (fds[FD_IPC].revents & POLLIN)) {
            if (ipc_dispatch(ipc)) surface_mgr_mark_dirty_all(&mgr);
        }
        if (nd && fds[FD_DBUS].fd >= 0 &&
            (fds[FD_DBUS].revents & (POLLIN | POLLHUP))) {
            notify_daemon_dispatch(nd);
        }
        if (mpris && fds[FD_MPRIS].fd >= 0 &&
            (fds[FD_MPRIS].revents & (POLLIN | POLLHUP))) {
            if (mpris_dispatch(mpris)) surface_mgr_mark_dirty_all(&mgr);
        }
        continue;
wl_error:
        report_display_error(display);
        rc = 1;
        break;
    }

    log_info("shutting down...");
    if (tfd >= 0) close(tfd);
    if (g_idle) idle_manager_destroy(g_idle);
    if (mpris) mpris_destroy(mpris);
    if (niri) niri_ipc_destroy(niri);
    if (systray) systray_destroy(systray);
    if (clip) clipboard_destroy(clip);
    if (ipc) ipc_destroy(ipc);
    if (hk) hotkeys_destroy(hk);
    if (sa) session_actions_destroy(sa);
    if (lock) lock_screen_destroy(lock);
    if (nd) notify_daemon_destroy(nd);
    if (wp) wallpaper_destroy(wp);
    if (osd) osd_destroy(osd);
    if (cc) control_center_destroy(cc);
    if (npanel) network_panel_destroy(npanel);
    if (btpanel) bluetooth_panel_destroy(btpanel);
    if (dock) dock_destroy(dock);
    if (g_dw) desktop_widgets_destroy(g_dw);
    if (calpanel) calendar_panel_destroy(calpanel);
    if (lpanel) launcher_panel_destroy(lpanel);
    if (launcher) launcher_destroy(launcher);

cleanup:
    surface_mgr_fini(&mgr);
    if (ctx.seat) seat_destroy(ctx.seat);
    if (wm) workspaces_destroy(wm);
    if (cw) config_watcher_destroy(cw);
    struct output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &outputs, link) {
        wl_list_remove(&out->link);
        output_destroy(out);
        free(out);
    }
    if (ctx.workspace_manager) ext_workspace_manager_v1_destroy(ctx.workspace_manager);
    if (ctx.layer_shell) zwlr_layer_shell_v1_destroy(ctx.layer_shell);
    if (ctx.shm) wl_shm_destroy(ctx.shm);
    if (ctx.compositor) wl_compositor_destroy(ctx.compositor);
    if (ctx.registry) wl_registry_destroy(ctx.registry);
    wl_display_disconnect(display);
    log_info("shutdown complete");
    return rc;
}


struct wallpaper *wallpaper_instance_get(void)
{
    return g_wp;
}