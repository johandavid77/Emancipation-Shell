#include "bar/workspaces.h"
#include "core/wayland.h"
#include "util/log.h"
#include "ext-workspace-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>

struct workspace {
    struct ext_workspace_handle_v1 *handle;
    char name[256];
    char id[128];
    bool active;
    bool visible;
    bool urgent;
    struct wl_list link;
};

struct workspace_group {
    struct ext_workspace_group_handle_v1 *handle;
    struct wl_list workspaces;
    struct wl_list link;
};

struct workspace_manager {
    struct wayland_ctx *ctx;
    struct ext_workspace_manager_v1 *mgr;
    struct wl_list groups;
    bool stopped;
};

static void workspace_handle_name(void *data,
                                  struct ext_workspace_handle_v1 *handle,
                                  const char *name)
{
    (void)handle;
    struct workspace *ws = (struct workspace *)data;
    if (ws && name) {
        strncpy(ws->name, name, sizeof(ws->name) - 1);
        ws->name[sizeof(ws->name) - 1] = '\0';
    }
}

static void workspace_handle_id(void *data,
                                struct ext_workspace_handle_v1 *handle,
                                const char *id)
{
    (void)handle;
    struct workspace *ws = (struct workspace *)data;
    if (ws && id) {
        strncpy(ws->id, id, sizeof(ws->id) - 1);
        ws->id[sizeof(ws->id) - 1] = '\0';
    }
}

static void workspace_handle_state(void *data,
                                   struct ext_workspace_handle_v1 *handle,
                                   uint32_t state)
{
    (void)handle;
    struct workspace *ws = (struct workspace *)data;
    if (!ws || !state) return;
    ws->active = false;
    ws->visible = false;
    ws->urgent = false;
    ws->active = (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
    ws->urgent = (state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT) != 0;
    ws->visible = (state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN) == 0;
}

static void workspace_handle_coordinates(void *data,
                                         struct ext_workspace_handle_v1 *handle,
                                         struct wl_array *coordinates)
{
    (void)data;
    (void)handle;
    (void)coordinates;
}

static void workspace_handle_capabilities(void *data,
                                         struct ext_workspace_handle_v1 *handle,
                                         uint32_t capabilities)
{
    (void)data;
    (void)handle;
    (void)capabilities;
}

static void workspace_handle_removed(void *data,
                                    struct ext_workspace_handle_v1 *handle)
{
    (void)handle;
    struct workspace *ws = (struct workspace *)data;
    if (ws) {
        wl_list_remove(&ws->link);
        ext_workspace_handle_v1_destroy(handle);
        free(ws);
    }
}

static const struct ext_workspace_handle_v1_listener workspace_handle_listener = {
    .name = workspace_handle_name,
    .id = workspace_handle_id,
    .state = workspace_handle_state,
    .coordinates = workspace_handle_coordinates,
    .capabilities = workspace_handle_capabilities,
    .removed = workspace_handle_removed,
};
static void workspace_group_handle_capabilities(void *data,
                                                struct ext_workspace_group_handle_v1 *group,
                                                uint32_t capabilities)
{
    (void)data;
    (void)group;
    (void)capabilities;
}

static void workspace_group_handle_output_enter(void *data,
                                                struct ext_workspace_group_handle_v1 *group,
                                                struct wl_output *output)
{
    (void)data;
    (void)group;
    (void)output;
}

static void workspace_group_handle_output_leave(void *data,
                                                struct ext_workspace_group_handle_v1 *group,
                                                struct wl_output *output)
{
    (void)data;
    (void)group;
    (void)output;
}

static void workspace_group_handle_workspace_enter(void *data,
                                                   struct ext_workspace_group_handle_v1 *group,
                                                   struct ext_workspace_handle_v1 *workspace)
{
    (void)group;
    struct workspace_group *wg = (struct workspace_group *)data;
    struct workspace *ws = calloc(1, sizeof(*ws));
    if (!ws) return;
    ws->handle = workspace;
    wl_list_init(&ws->link);
    ext_workspace_handle_v1_add_listener(workspace, &workspace_handle_listener, ws);
    wl_list_insert(&wg->workspaces, &ws->link);
    log_debug("workspace added to group");
}

static void workspace_group_handle_workspace_leave(void *data,
                                                   struct ext_workspace_group_handle_v1 *group,
                                                   struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)group;
    (void)workspace;
}

static void workspace_group_handle_removed(void *data,
                                           struct ext_workspace_group_handle_v1 *group)
{
    (void)group;
    struct workspace_group *wg = (struct workspace_group *)data;
    if (!wg) return;
    struct workspace *ws, *wstmp;
    wl_list_for_each_safe(ws, wstmp, &wg->workspaces, link) {
        wl_list_remove(&ws->link);
        if (ws->handle) ext_workspace_handle_v1_destroy(ws->handle);
        free(ws);
    }
    wl_list_remove(&wg->link);
    ext_workspace_group_handle_v1_destroy(group);
    free(wg);
}

static const struct ext_workspace_group_handle_v1_listener workspace_group_listener = {
    .capabilities = workspace_group_handle_capabilities,
    .output_enter = workspace_group_handle_output_enter,
    .output_leave = workspace_group_handle_output_leave,
    .workspace_enter = workspace_group_handle_workspace_enter,
    .workspace_leave = workspace_group_handle_workspace_leave,
    .removed = workspace_group_handle_removed,
};
static void workspace_manager_workspace_group(void *data,
                                              struct ext_workspace_manager_v1 *manager,
                                              struct ext_workspace_group_handle_v1 *group)
{
    (void)manager;
    struct workspace_manager *wm = (struct workspace_manager *)data;
    struct workspace_group *wg = calloc(1, sizeof(*wg));
    if (!wg) return;
    wg->handle = group;
    wl_list_init(&wg->workspaces);
    wl_list_init(&wg->link);
    ext_workspace_group_handle_v1_add_listener(group, &workspace_group_listener, wg);
    wl_list_insert(&wm->groups, &wg->link);
    log_debug("workspace group added");
}

static void workspace_manager_finished(void *data,
                                      struct ext_workspace_manager_v1 *manager)
{
    (void)manager;
    struct workspace_manager *wm = (struct workspace_manager *)data;
    wm->stopped = true;
    log_info("ext_workspace_manager_v1 finished");
}

static void workspace_manager_workspace(void *data,
                                       struct ext_workspace_manager_v1 *manager,
                                       struct ext_workspace_handle_v1 *workspace)
{
    (void)data;
    (void)manager;
    (void)workspace;
}

static void workspace_manager_done(void *data,
                                  struct ext_workspace_manager_v1 *manager)
{
    (void)data;
    (void)manager;
}

static const struct ext_workspace_manager_v1_listener workspace_manager_listener = {
    .workspace_group = workspace_manager_workspace_group,
    .workspace = workspace_manager_workspace,
    .done = workspace_manager_done,
    .finished = workspace_manager_finished,
};

struct workspace_manager *workspaces_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct workspace_manager *wm = calloc(1, sizeof(*wm));
    if (!wm) return NULL;
    wm->ctx = ctx;
    wm->mgr = NULL;
    wl_list_init(&wm->groups);
    wm->stopped = false;
    return wm;
}

void workspaces_bind(struct workspace_manager *wm)
{
    if (!wm || !wm->ctx || !wm->ctx->workspace_manager) return;
    wm->mgr = wm->ctx->workspace_manager;
    ext_workspace_manager_v1_add_listener(wm->mgr, &workspace_manager_listener, wm);
}

void workspaces_destroy(struct workspace_manager *wm)
{
    if (!wm) return;
    struct workspace_group *wg, *wgtmp;
    wl_list_for_each_safe(wg, wgtmp, &wm->groups, link) {
        struct workspace *ws, *wstmp;
        wl_list_for_each_safe(ws, wstmp, &wg->workspaces, link) {
            wl_list_remove(&ws->link);
            if (ws->handle) ext_workspace_handle_v1_destroy(ws->handle);
            free(ws);
        }
        wl_list_remove(&wg->link);
        if (wg->handle) ext_workspace_group_handle_v1_destroy(wg->handle);
        free(wg);
    }
    wm->mgr = NULL;
    free(wm);
}
