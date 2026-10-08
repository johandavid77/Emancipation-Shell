#include "notify/toast.h"
#include "core/panel.h"
#include "core/output.h"
#include "util/log.h"
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOAST_W 320
#define TOAST_H 64
#define TOAST_PAD 12

struct toast_panel {
    struct panel panel;
    struct output *fallback_out;
    struct timespec hide_at;
    struct notification cur;
};

static void ts_now(struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static void toast_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct toast_panel *t = p->userdata;
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.98);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 1,1,1,0.95);
    cairo_move_to(cr, TOAST_PAD, TOAST_PAD);
    cairo_show_text(cr, t->cur.summary[0] ? t->cur.summary : "Notification");
    cairo_set_source_rgba(cr, 1,1,1,0.7);
    cairo_move_to(cr, TOAST_PAD, TOAST_PAD + 16);
    const char *b = t->cur.body;
    char line[128];
    if (b && b[0]) {
        strncpy(line, b, sizeof(line)-1);
        line[sizeof(line)-1] = 0;
        if (strlen(line) > 40) { line[37]='.'; line[38]='.'; line[39]='.'; line[40]=0; }
    } else {
        line[0] = 0;
    }
    if (line[0]) cairo_show_text(cr, line);
}

static bool toast_key(struct panel *p, uint32_t k, uint32_t m)
{
    (void)p;(void)k;(void)m; return true;
}
static bool toast_click(struct panel *p, double x,double y)
{
    (void)x;(void)y; panel_hide(p); return true;
}
static void toast_motion(struct panel *p,double x,double y){(void)p;(void)x;(void)y;}

struct toast_panel *toast_create(struct wayland_ctx *ctx, struct output *out)
{
    struct toast_panel *t = calloc(1,sizeof(*t));
    if(!t)return NULL;
    panel_init(&t->panel, ctx);
    t->fallback_out = out;
    panel_set_callbacks(&t->panel, toast_draw, toast_key, toast_click, toast_motion, t);
    return t;
}
void toast_destroy(struct toast_panel *t){ if(!t)return; panel_fini(&t->panel); free(t); }
void toast_show(struct toast_panel *t, const struct notification *n, int timeout_ms)
{
    if(!t||!n)return;
    t->cur = *n;
    ts_now(&t->hide_at);
    t->hide_at.tv_sec += timeout_ms/1000;
    t->hide_at.tv_nsec += (timeout_ms%1000)*1000000;
    if(t->hide_at.tv_nsec>=1000000000){t->hide_at.tv_sec+=1;t->hide_at.tv_nsec-=1000000000;}
    struct output *out = t->panel.out ? t->panel.out : t->fallback_out;
    panel_show(&t->panel, out, TOAST_W, TOAST_H);
}
void toast_tick(struct toast_panel *t)
{
    if(!t||!panel_is_visible(&t->panel))return;
    struct timespec now; ts_now(&now);
    if(now.tv_sec>t->hide_at.tv_sec || (now.tv_sec==t->hide_at.tv_sec && now.tv_nsec>=t->hide_at.tv_nsec)){
        panel_hide(&t->panel);
    }
}
