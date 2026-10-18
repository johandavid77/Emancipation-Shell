#ifndef EMANCIPATION_BACKENDS_H
#define EMANCIPATION_BACKENDS_H

#include <stdbool.h>
#include <time.h>

struct config;

enum net_kind {
    NET_NONE = 0,
    NET_ETHERNET,
    NET_WIFI,
    NET_OTHER,
};

#define BACKENDS_MAX_SCRIPTS 8
#define BACKENDS_SCRIPT_CMD 160
#define BACKENDS_SCRIPT_OUT 256

struct script_widget {
    char cmd[BACKENDS_SCRIPT_CMD];
    char out[BACKENDS_SCRIPT_OUT];
    int interval; /* seconds between refreshes */
    double last;  /* monotonic seconds of the last run */
    bool ran;
};

/* Polled system state for the bar widgets (volume, brightness, network,
 * media, clipboard count and user-defined script widgets). */
struct backends_state {
    /* volume */
    bool vol_available;
    int vol_pct;   /* 0..100 */
    bool vol_muted;
    char vol_backend[16]; /* "", "wpctl", "pactl", "amixer" */

    /* brightness */
    bool br_available;
    int br_pct; /* 0..100 */

    /* network */
    enum net_kind net_kind;
    bool net_connected;
    char net_if[32];
    char net_ssid[64];
    int net_strength; /* 0..100, -1 = unknown */

    /* media (MPRIS) */
    bool media_present;
    bool media_playing;
    char media_title[160];
    char media_artist[128];
    char media_player[64];

    /* clipboard */
    int clipboard_count;

    /* script-backed custom widgets */
    struct script_widget scripts[BACKENDS_MAX_SCRIPTS];
    int n_scripts;

    /* internal throttles */
    double t_fast; /* volume/brightness/network */
    double t_probe;
    char bl_path[256]; /* /sys/class/backlight/<dev> */
};

void backends_init(struct backends_state *b);

/* Poll all sources; `clipboard_count` is supplied by the caller. Returns true
 * when some visible value changed (so the caller can mark the bar dirty). */
bool backends_update(struct backends_state *b, const struct config *cfg, int clipboard_count);

/* Look up the cached output of a script widget by its command string. */
const char *backends_script_output(const struct backends_state *b, const char *cmd);

/* True if `name` is a built-in bar module (i.e. not a custom script command). */
bool backends_is_builtin_module(const char *name);

/* Volume/brightness controls used by the IPC and OSD paths. */
void backends_volume_step(struct backends_state *b, int delta_pct);
void backends_volume_toggle_mute(struct backends_state *b);
void backends_brightness_step(struct backends_state *b, int delta_pct);

#endif /* EMANCIPATION_BACKENDS_H */
