#include "config/config.h"
#include <string.h>

void config_defaults(struct config *cfg)
{
    if (!cfg) return;
    cfg->bar.height = 32;
    cfg->bar.visible = true;
    cfg->theme.background.r = 0.1f;
    cfg->theme.background.g = 0.1f;
    cfg->theme.background.b = 0.1f;
    cfg->theme.background.a = 1.0f;
    cfg->theme.foreground.r = 0.95f;
    cfg->theme.foreground.g = 0.95f;
    cfg->theme.foreground.b = 0.95f;
    cfg->theme.foreground.a = 1.0f;
    strncpy(cfg->font.family, "Sans", sizeof(cfg->font.family)-1);
    cfg->font.family[sizeof(cfg->font.family)-1] = '\0';
    cfg->font.size = 12;
}

bool config_equal(const struct config *a, const struct config *b)
{
    if (!a || !b) return false;
    if (a->bar.height != b->bar.height) return false;
    if (a->bar.visible != b->bar.visible) return false;
    if (a->theme.background.r != b->theme.background.r ||
        a->theme.background.g != b->theme.background.g ||
        a->theme.background.b != b->theme.background.b ||
        a->theme.background.a != b->theme.background.a) return false;
    if (a->theme.foreground.r != b->theme.foreground.r ||
        a->theme.foreground.g != b->theme.foreground.g ||
        a->theme.foreground.b != b->theme.foreground.b ||
        a->theme.foreground.a != b->theme.foreground.a) return false;
    if (strcmp(a->font.family, b->font.family) != 0) return false;
    if (a->font.size != b->font.size) return false;
    return true;
}
