#include "bar/workspaces.h"
#include "core/wayland.h"
#include "util/log.h"
#include "ext-workspace-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define WS_MAX_OUTPUTS 8

struct workspace_group {
    struct ext_workspace_group_handle_v1 *handle;
    struct wl_output *outputs[WS_MAX_OUTPUTS];
    size_t n_outputs;
    struct wl_list link;
};

struct workspace {
    struct ext_workspace_handle_v1 *handle;
    struct workspace_group *group; /* may be NULL */
    char name[64];
    char id[64];
    uint32_t coord[2];
    size_t n_coord;
    uint32_t state;
    struct wl_list link;
};

struct workspace_manager {
    struct wayland_ctx *ctx;
    struct ext_workspace_manager_v1 *mgr;
    struct wl_list groups;     /* workspace_group.link */
    struct wl_list workspaces; /* workspace.link */
    workspaces_changed_cb cb;
    void *cb_data;
    bool finished;
};

/* ---- workspace handle ---------------------------------------------------- */

static void ws_id(void *data, struct ext_workspace_handle_v1 *h, const char *id)
{
    (void)h;
    struct workspace *ws = data;
    if (id) snprintf(ws->id, sizeof(ws->id), "%s", id);
    else ws->id[0] = 0;
}

static void ws_name(void *data, struct ext_workspace_handle_v1 *h, const char *name)
{
    (void)h;
    struct workspace *ws = data;
    snprintf(ws->name, sizeof(ws->name), "%s", name ? name : "");
}

static void ws_coordinates(void *data, struct ext_workspace_handle_v1 *h, struct wl_array *coords)
{
    (void)h;
    struct workspace *ws = data;
    ws->n_coord = 0;
    uint32_t *c;
    wl_array_for_each(c, coords) {
        if (ws->n_coord >= 2) break;
        ws->coord[ws->n_coord++] = *c;
    }
}

static void ws_state(void *data, struct ext_workspace_handle_v1 *h, uint32_t state)
{
    (void)h;
    struct workspace *ws = data;
    ws->state = state; /* 0 is a valid value: clears every flag */
}

static void ws_capabilities(void *data, struct ext_workspace_handle_v1 *h, uint32_t caps)
{
    (void)data; (void)h; (void)caps;
}

static void ws_free(struct workspace *ws)
{
    wl_list_remove(&ws->link);
    if (ws->handle) ext_workspace_handle_v1_destroy(ws->handle);
    free(ws);
}

static void ws_removed(void *data, struct ext_workspace_handle_v1 *h)
{
    (void)h;
    ws_free(data);
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id = ws_id,
    .name = ws_name,
    .coordinates = ws_coordinates,
    .state = ws_state,
    .capabilities = ws_capabilities,
    .removed = ws_removed,
};

/* ---- group handle -------------------------------------------------------- */

static struct workspace *find_ws(struct workspace_manager *wm, struct ext_workspace_handle_v1 *h)
{
    struct workspace *ws;
    wl_list_for_each(ws, &wm->workspaces, link) {
        if (ws->handle == h) return ws;
    }
    return NULL;
}

struct group_ctx {
    struct workspace_manager *wm;
    struct workspace_group *g;
};

static void grp_capabilities(void *data, struct ext_workspace_group_handle_v1 *h, uint32_t caps)
{
    (void)data; (void)h; (void)caps;
}

static void grp_output_enter(void *data, struct ext_workspace_group_handle_v1 *h, struct wl_output *o)
{
    (void)h;
    struct group_ctx *gc = data;
    struct workspace_group *g = gc->g;
    for (size_t i = 0; i < g->n_outputs; i++) {
        if (g->outputs[i] == o) return;
    }
    if (g->n_outputs < WS_MAX_OUTPUTS) g->outputs[g->n_outputs++] = o;
}

static void grp_output_leave(void *data, struct ext_workspace_group_handle_v1 *h, struct wl_output *o)
{
    (void)h;
    struct group_ctx *gc = data;
    struct workspace_group *g = gc->g;
    for (size_t i = 0; i < g->n_outputs; i++) {
        if (g->outputs[i] == o) {
            g->outputs[i] = g->outputs[--g->n_outputs];
            return;
        }
    }
}

static void grp_workspace_enter(void *data, struct ext_workspace_group_handle_v1 *h,
                                struct ext_workspace_handle_v1 *wsh)
{
    (void)h;
    struct group_ctx *gc = data;
    struct workspace *ws = find_ws(gc->wm, wsh);
    if (ws) ws->group = gc->g;
}

static void grp_workspace_leave(void *data, struct ext_workspace_group_handle_v1 *h,
                                struct ext_workspace_handle_v1 *wsh)
{
    (void)h;
    struct group_ctx *gc = data;
    struct workspace *ws = find_ws(gc->wm, wsh);
    if (ws && ws->group == gc->g) ws->group = NULL;
}

static void grp_free(struct workspace_manager *wm, struct workspace_group *g)
{
    struct workspace *ws;
    wl_list_for_each(ws, &wm->workspaces, link) {
        if (ws->group == g) ws->group = NULL;
    }
    struct group_ctx *gc = g->handle ? ext_workspace_group_handle_v1_get_user_data(g->handle) : NULL;
    wl_list_remove(&g->link);
    if (g->handle) ext_workspace_group_handle_v1_destroy(g->handle);
    free(gc);
    free(g);
}

static void grp_removed(void *data, struct ext_workspace_group_handle_v1 *h)
{
    (void)h;
    struct group_ctx *gc = data;
    grp_free(gc->wm, gc->g);
}

static const struct ext_workspace_group_handle_v1_listener grp_listener = {
    .capabilities = grp_capabilities,
    .output_enter = grp_output_enter,
    .output_leave = grp_output_leave,
    .workspace_enter = grp_workspace_enter,
    .workspace_leave = grp_workspace_leave,
    .removed = grp_removed,
};

/* ---- manager ------------------------------------------------------------- */

static void mgr_workspace_group(void *data, struct ext_workspace_manager_v1 *m,
                                struct ext_workspace_group_handle_v1 *h)
{
    (void)m;
    struct workspace_manager *wm = data;
    struct workspace_group *g = calloc(1, sizeof(*g));
    struct group_ctx *gc = calloc(1, sizeof(*gc));
    if (!g || !gc) {
        free(g);
        free(gc);
        ext_workspace_group_handle_v1_destroy(h);
        return;
    }
    g->handle = h;
    gc->wm = wm;
    gc->g = g;
    ext_workspace_group_handle_v1_add_listener(h, &grp_listener, gc);
    wl_list_insert(wm->groups.prev, &g->link);
}

static void mgr_workspace(void *data, struct ext_workspace_manager_v1 *m,
                          struct ext_workspace_handle_v1 *h)
{
    (void)m;
    struct workspace_manager *wm = data;
    struct workspace *ws = calloc(1, sizeof(*ws));
    if (!ws) {
        ext_workspace_handle_v1_destroy(h);
        return;
    }
    ws->handle = h;
    ext_workspace_handle_v1_add_listener(h, &ws_listener, ws);
    wl_list_insert(wm->workspaces.prev, &ws->link);
}

static void mgr_done(void *data, struct ext_workspace_manager_v1 *m)
{
    (void)m;
    struct workspace_manager *wm = data;
    if (wm->cb) wm->cb(wm->cb_data);
}

static void mgr_finished(void *data, struct ext_workspace_manager_v1 *m)
{
    (void)m;
    struct workspace_manager *wm = data;
    wm->finished = true;
    log_info("ext_workspace_manager_v1 finished");
}

static const struct ext_workspace_manager_v1_listener mgr_listener = {
    .workspace_group = mgr_workspace_group,
    .workspace = mgr_workspace,
    .done = mgr_done,
    .finished = mgr_finished,
};

/* ---- public -------------------------------------------------------------- */

struct workspace_manager *workspaces_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct workspace_manager *wm = calloc(1, sizeof(*wm));
    if (!wm) return NULL;
    wm->ctx = ctx;
    wl_list_init(&wm->groups);
    wl_list_init(&wm->workspaces);
    return wm;
}

void workspaces_bind(struct workspace_manager *wm)
{
    if (!wm || wm->mgr || !wm->ctx || !wm->ctx->workspace_manager) return;
    wm->mgr = wm->ctx->workspace_manager;
    ext_workspace_manager_v1_add_listener(wm->mgr, &mgr_listener, wm);
}

void workspaces_set_changed_cb(struct workspace_manager *wm, workspaces_changed_cb cb, void *userdata)
{
    if (!wm) return;
    wm->cb = cb;
    wm->cb_data = userdata;
}

static int ws_cmp(const struct workspace *a, const struct workspace *b)
{
    size_t n = a->n_coord < b->n_coord ? a->n_coord : b->n_coord;
    for (size_t i = 0; i < n; i++) {
        if (a->coord[i] != b->coord[i]) return a->coord[i] < b->coord[i] ? -1 : 1;
    }
    return strcmp(a->name, b->name);
}

/* Leading number of a workspace id/name, like Noctalia's label fallback
 * (workspaces_widget.cpp:1413-1435). Returns 0 when there is none. */
static unsigned long label_number(const char *s)
{
    if (!s) return 0;
    while (*s == ' ' || *s == '-') s++;
    if (*s < '0' || *s > '9') return 0;
    return strtoul(s, NULL, 10);
}

size_t workspaces_snapshot(struct workspace_manager *wm, struct wl_output *output,
                           struct workspace_info *out, size_t max)
{
    if (!wm || !out || max == 0) return 0;
    const struct workspace *sel[64];
    size_t n = 0;
    struct workspace *ws;
    wl_list_for_each(ws, &wm->workspaces, link) {
        if (output && ws->group) {
            bool on = false;
            for (size_t i = 0; i < ws->group->n_outputs; i++) {
                if (ws->group->outputs[i] == output) { on = true; break; }
            }
            if (!on) continue;
        }
        if (n >= sizeof(sel) / sizeof(sel[0]) || n >= max) break;
        /* insertion sort: lists are tiny */
        size_t i = n++;
        while (i > 0 && ws_cmp(sel[i - 1], ws) > 0) {
            sel[i] = sel[i - 1];
            i--;
        }
        sel[i] = ws;
    }
    for (size_t i = 0; i < n; i++) {
        snprintf(out[i].name, sizeof(out[i].name), "%s", sel[i]->name);
        snprintf(out[i].id, sizeof(out[i].id), "%s", sel[i]->id);
        if (sel[i]->name[0]) snprintf(out[i].name, sizeof(out[i].name), "%s", sel[i]->name);
        else if (sel[i]->id[0]) snprintf(out[i].name, sizeof(out[i].name), "%s", sel[i]->id);
        else out[i].name[0] = 0;
        unsigned long num = label_number(sel[i]->name[0] ? sel[i]->name : sel[i]->id);
        if (sel[i]->name[0]) snprintf(out[i].label, sizeof(out[i].label), "%s", sel[i]->name);
        else if (num) snprintf(out[i].label, sizeof(out[i].label), "%lu", num);
        else if (sel[i]->id[0]) snprintf(out[i].label, sizeof(out[i].label), "%s", sel[i]->id);
        else snprintf(out[i].label, sizeof(out[i].label), "%zu", i + 1);
        out[i].active = sel[i]->state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
        out[i].urgent = sel[i]->state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT;
        out[i].hidden = sel[i]->state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN;
        out[i].ref = sel[i]->handle;
    }
    if (getenv("ESH_WS_DEBUG")) {
        for (size_t i = 0; i < n; i++) {
            log_info("ws[%zu] id=%s name=%s active=%d urgent=%d hidden=%d",
                     i, out[i].id, out[i].name, out[i].active, out[i].urgent, out[i].hidden);
        }
    }
    return n;
}

void workspaces_activate(struct workspace_manager *wm, void *ref)
{
    if (!wm || !wm->mgr || !ref) return;
    struct workspace *ws = find_ws(wm, ref);
    if (!ws) return; /* removed since the snapshot */
    ext_workspace_handle_v1_activate(ws->handle);
    ext_workspace_manager_v1_commit(wm->mgr);
}

void workspaces_step(struct workspace_manager *wm, struct wl_output *output, int dir)
{
    struct workspace_info ws[64];
    size_t n = workspaces_snapshot(wm, output, ws, 64);
    for (size_t i = 0; i < n; i++) {
        if (!ws[i].active) continue;
        if (dir < 0 && i > 0) workspaces_activate(wm, ws[i - 1].ref);
        if (dir > 0 && i + 1 < n) workspaces_activate(wm, ws[i + 1].ref);
        return;
    }
}

void workspaces_destroy(struct workspace_manager *wm)
{
    if (!wm) return;
    struct workspace *ws, *wtmp;
    wl_list_for_each_safe(ws, wtmp, &wm->workspaces, link) {
        ws_free(ws);
    }
    struct workspace_group *g, *gtmp;
    wl_list_for_each_safe(g, gtmp, &wm->groups, link) {
        grp_free(wm, g);
    }
    /* the manager proxy itself is owned by wayland_ctx */
    free(wm);
}
