#ifndef EMANCIPATION_DESKTOP_WIDGET_H
#define EMANCIPATION_DESKTOP_WIDGET_H

#include <stdbool.h>

struct desktop_widget {
    char id[64];
    char name[128];
    bool visible;
    int x;
    int y;
    int w;
    int h;
};

struct desktop_widget_mgr {
    struct desktop_widget *widgets;
    int count;
    int capacity;
};

void dw_mgr_init(struct desktop_widget_mgr *m);
void dw_mgr_free(struct desktop_widget_mgr *m);
bool dw_mgr_add(struct desktop_widget_mgr *m, const char *id, const char *name);
bool dw_mgr_toggle(struct desktop_widget_mgr *m, const char *id);

#endif /* EMANCIPATION_DESKTOP_WIDGET_H */
