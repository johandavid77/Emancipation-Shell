#include "widgets/desktop_widget.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

void dw_mgr_init(struct desktop_widget_mgr *m)
{
    if (!m) return;
    m->widgets = NULL;
    m->count = 0;
    m->capacity = 0;
}

void dw_mgr_free(struct desktop_widget_mgr *m)
{
    if (!m) return;
    free(m->widgets);
    m->widgets = NULL;
    m->count = m->capacity = 0;
}

bool dw_mgr_add(struct desktop_widget_mgr *m, const char *id, const char *name)
{
    if (!m || !id || !name) return false;
    if (m->count >= m->capacity) {
        int nc = m->capacity == 0 ? 4 : m->capacity * 2;
        struct desktop_widget *nw = realloc(m->widgets, nc * sizeof(*nw));
        if (!nw) return false;
        m->widgets = nw;
        m->capacity = nc;
    }
    strncpy(m->widgets[m->count].id, id, sizeof(m->widgets[m->count].id)-1);
    m->widgets[m->count].id[sizeof(m->widgets[m->count].id)-1] = '\0';
    strncpy(m->widgets[m->count].name, name, sizeof(m->widgets[m->count].name)-1);
    m->widgets[m->count].name[sizeof(m->widgets[m->count].name)-1] = '\0';
    m->widgets[m->count].visible = false;
    m->widgets[m->count].x = 100;
    m->widgets[m->count].y = 100;
    m->widgets[m->count].w = 200;
    m->widgets[m->count].h = 120;
    m->count++;
    log_info("desktop widget added: %s", name);
    return true;
}

bool dw_mgr_toggle(struct desktop_widget_mgr *m, const char *id)
{
    if (!m || !id) return false;
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->widgets[i].id, id) == 0) {
            m->widgets[i].visible = !m->widgets[i].visible;
            log_info("desktop widget %s %s", id, m->widgets[i].visible ? "show" : "hide");
            return true;
        }
    }
    return false;
}
