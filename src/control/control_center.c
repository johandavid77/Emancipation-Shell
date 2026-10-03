#include "control/control_center.h"
#include "core/output.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

struct control_center {
    struct wayland_ctx *ctx;
    struct output *out;
    bool visible;
    bool wifi;
    bool bluetooth;
    bool dark_mode;
    bool mute;
    int power_profile;
};

struct control_center *control_center_create(struct wayland_ctx *ctx, struct output *out)
{
    if (!ctx || !out) return NULL;
    struct control_center *cc = calloc(1, sizeof(*cc));
    if (!cc) return NULL;
    cc->ctx = ctx;
    cc->out = out;
    cc->visible = false;
    cc->wifi = true;
    cc->bluetooth = false;
    cc->dark_mode = false;
    cc->mute = false;
    cc->power_profile = 0;
    log_debug("control center created");
    return cc;
}

void control_center_destroy(struct control_center *cc)
{
    if (!cc) return;
    free(cc);
}

void control_center_show(struct control_center *cc)
{
    if (!cc) return;
    cc->visible = true;
    log_info("control center shown (slide-in)");
}

void control_center_hide(struct control_center *cc)
{
    if (!cc) return;
    cc->visible = false;
    log_info("control center hidden");
}

void control_center_toggle(struct control_center *cc)
{
    if (!cc) return;
    if (cc->visible) control_center_hide(cc);
    else control_center_show(cc);
}

bool control_center_is_visible(struct control_center *cc)
{
    if (!cc) return false;
    return cc->visible;
}

void control_center_set_toggle(struct control_center *cc, const char *name, bool enabled)
{
    if (!cc || !name) return;
    if (strcmp(name, "wifi") == 0) {
        cc->wifi = enabled;
        log_info("wifi %s (nm/connman)", enabled ? "on" : "off");
    } else if (strcmp(name, "bluetooth") == 0) {
        cc->bluetooth = enabled;
        log_info("bluetooth %s (bluez)", enabled ? "on" : "off");
    } else if (strcmp(name, "dark_mode") == 0) {
        cc->dark_mode = enabled;
        log_info("dark_mode %s", enabled ? "on" : "off");
    } else if (strcmp(name, "mute") == 0) {
        cc->mute = enabled;
        log_info("audio mute %s", enabled ? "on" : "off");
    } else if (strcmp(name, "power_profile") == 0) {
        cc->power_profile = enabled ? 1 : 0;
        log_info("power_profile %d", cc->power_profile);
    } else {
        log_warn("unknown toggle: %s", name);
    }
}
