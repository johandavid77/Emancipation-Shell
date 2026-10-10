#include "systray/systray.h"
#include "launcher/icons.h"
#include "util/log.h"
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

#define SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE "org.kde.StatusNotifierItem"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"

#define SYSTRAY_MAX_ITEMS 16
#define SNI_ICON_PX 18

struct sni_item {
    char service[256]; /* unique bus name, e.g. ":1.42" */
    char path[512];
    char icon_name[256];
    cairo_surface_t *icon;
    struct systray *systray;
};

struct systray {
    struct wayland_ctx *ctx;
    DBusConnection *conn;
    bool owns_watcher;
    bool dirty;
    struct sni_item items[16];
    int n_items;
};

/* ---- D-Bus helpers ------------------------------------------------------ */

static void update_registered_property(struct systray *s)
{
    /* Best-effort: emit PropertiesChanged so hosts update their list. */
    if (!s->conn) return;
    DBusMessage *m = dbus_message_new_signal(
        "/StatusNotifierWatcher", "org.freedesktop.DBus.Properties",
        "PropertiesChanged");
    if (!m) return;
    const char *iface = SNI_WATCHER_IFACE;
    DBusMessageIter it, arr, entry, var, astr;
    dbus_message_iter_init_append(m, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
    {
        const char *key = "RegisteredStatusNotifierItems";
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &astr);
        for (int i = 0; i < s->n_items; i++) {
            const char *svc = s->items[i].service;
            dbus_message_iter_append_basic(&astr, DBUS_TYPE_STRING, &svc);
        }
        dbus_message_iter_close_container(&var, &astr);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
    }
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(s->conn, m, NULL);
    dbus_message_unref(m);
}

static void on_icon_reply(DBusPendingCall *pc, void *user_data)
{
    struct sni_item *item = user_data;
    DBusMessage *reply = dbus_pending_call_steal_reply(pc);
    if (!reply) { log_warn("systray icon: no reply"); return; }
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        log_warn("systray icon: error reply %s",
                 dbus_message_get_error_name(reply));
    }
    DBusMessageIter it, var;
    if (dbus_message_iter_init(reply, &it) &&
        dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&it, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
            const char *v = NULL;
            dbus_message_iter_get_basic(&var, &v);
            if (v && v[0]) {
                snprintf(item->icon_name, sizeof(item->icon_name), "%s", v);
                char *p = icon_resolve_path(item->icon_name);
                if (p) {
                    item->icon = icon_load_surface(p, 22);
                    free(p);
                    log_info("systray icon: %s -> %s", item->icon_name,
                             item->icon ? "loaded" : "no surface");
                }
                item->systray->dirty = true;
            }
        }
    }
    dbus_message_unref(reply);
}

static void query_icon_name(struct systray *s, struct sni_item *item)
{
    DBusMessage *msg = dbus_message_new_method_call(
        item->service, item->path, "org.freedesktop.DBus.Properties", "Get");
    if (!msg) return;
    const char *iface = "org.kde.StatusNotifierItem";
    const char *prop = "IconName";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    DBusPendingCall *pc = NULL;
    if (dbus_connection_send_with_reply(s->conn, msg, &pc, 2000) && pc) {
        item->systray = s;
        dbus_pending_call_set_notify(pc, on_icon_reply, item, NULL);
        dbus_pending_call_unref(pc);
        log_info("systray icon: querying %s %s IconName", item->service, item->path);
    } else {
        log_warn("systray icon: send_with_reply failed for %s", item->service);
    }
    dbus_message_unref(msg);
}

static void add_item(struct systray *s, const char *sender, const char *arg)
{
    char service[256], path[512];
    if (arg && arg[0] == '/') {
        snprintf(service, sizeof(service), "%s", sender ? sender : "");
        snprintf(path, sizeof(path), "%s", arg);
    } else if (arg && arg[0]) {
        snprintf(service, sizeof(service), "%s", arg);
        snprintf(path, sizeof(path), "/StatusNotifierItem");
    } else {
        snprintf(service, sizeof(service), "%s", sender ? sender : "");
        snprintf(path, sizeof(path), "/StatusNotifierItem");
    }
    if (!service[0]) return;
    for (int i = 0; i < s->n_items; i++)
        if (strcmp(s->items[i].service, service) == 0 &&
            strcmp(s->items[i].path, path) == 0)
            return;
    if (s->n_items >= 16) return;
    struct sni_item *it = &s->items[s->n_items++];
    memset(it, 0, sizeof(*it));
    snprintf(it->service, sizeof(it->service), "%s", service);
    snprintf(it->path, sizeof(it->path), "%s", path);
    query_icon_name(s, it);
    log_info("systray item registered: %s%s (%s)", service, path, it->icon_name);
    update_registered_property(s);
    s->dirty = true;
}

static void remove_item(struct systray *s, const char *service)
{
    for (int i = 0; i < s->n_items; i++) {
        if (strcmp(s->items[i].service, service) == 0) {
            if (s->items[i].icon) cairo_surface_destroy(s->items[i].icon);
            memmove(&s->items[i], &s->items[i + 1],
                    (s->n_items - i - 1) * sizeof(struct sni_item));
            s->n_items--;
            update_registered_property(s);
            s->dirty = true;
            return;
        }
    }
}

static void handle_properties(struct systray *s, DBusMessage *msg)
{
    DBusMessageIter it;
    if (!dbus_message_iter_init(msg, &it)) return;
    char *iface = NULL, *prop = NULL;
    DBusMessageIter cur = it;
    if (dbus_message_iter_get_arg_type(&cur) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&cur, &iface);
    if (dbus_message_iter_next(&cur) &&
        dbus_message_iter_get_arg_type(&cur) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&cur, &prop);
    (void)iface;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return;
    DBusMessageIter rit, var, astr;
    dbus_message_iter_init_append(reply, &rit);
    if (prop && strcmp(prop, "RegisteredStatusNotifierItems") == 0) {
        dbus_message_iter_open_container(&rit, DBUS_TYPE_VARIANT, "as", &var);
        dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &astr);
        for (int i = 0; i < s->n_items; i++) {
            const char *svc = s->items[i].service;
            dbus_message_iter_append_basic(&astr, DBUS_TYPE_STRING, &svc);
        }
        dbus_message_iter_close_container(&var, &astr);
        dbus_message_iter_close_container(&rit, &var);
    } else if (prop && strcmp(prop, "IsStatusNotifierHostRegistered") == 0) {
        dbus_bool_t v = TRUE;
        dbus_message_iter_open_container(&rit, DBUS_TYPE_VARIANT, "b", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &v);
        dbus_message_iter_close_container(&rit, &var);
    } else if (prop && strcmp(prop, "ProtocolVersion") == 0) {
        dbus_int32_t v = 0;
        dbus_message_iter_open_container(&rit, DBUS_TYPE_VARIANT, "i", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &v);
        dbus_message_iter_close_container(&rit, &var);
    } else {
        dbus_message_unref(reply);
        DBusMessage *err = dbus_message_new_error(
            msg, "org.freedesktop.DBus.Error.UnknownProperty",
            "Unknown property");
        if (err) { dbus_connection_send(s->conn, err, NULL); dbus_message_unref(err); }
        return;
    }
    dbus_connection_send(s->conn, reply, NULL);
    dbus_message_unref(reply);
}

static void handle_getall(struct systray *s, DBusMessage *msg)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return;
    DBusMessageIter rit, arr, entry, var, astr;
    dbus_message_iter_init_append(reply, &rit);
    dbus_message_iter_open_container(&rit, DBUS_TYPE_ARRAY, "{sv}", &arr);
    const char *key = "RegisteredStatusNotifierItems";
    dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &var);
    dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "s", &astr);
    for (int i = 0; i < s->n_items; i++) {
        const char *svc = s->items[i].service;
        dbus_message_iter_append_basic(&astr, DBUS_TYPE_STRING, &svc);
    }
    dbus_message_iter_close_container(&var, &astr);
    dbus_message_iter_close_container(&entry, &var);
    dbus_message_iter_close_container(&arr, &entry);
    dbus_message_iter_close_container(&rit, &arr);
    dbus_connection_send(s->conn, reply, NULL);
    dbus_message_unref(reply);
}

static DBusHandlerResult watcher_method(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    struct systray *s = ud;
    if (dbus_message_is_method_call(msg, SNI_WATCHER_IFACE, "RegisterStatusNotifierItem")) {
        const char *arg = NULL;
        DBusMessageIter it;
        if (dbus_message_iter_init(msg, &it) &&
            dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&it, &arg);
        add_item(s, dbus_message_get_sender(msg), arg);
        DBusMessage *r = dbus_message_new_method_return(msg);
        if (r) { dbus_connection_send(conn, r, NULL); dbus_message_unref(r); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, SNI_WATCHER_IFACE, "RegisterStatusNotifierHost")) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        if (r) { dbus_connection_send(conn, r, NULL); dbus_message_unref(r); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get")) {
        handle_properties(s, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll")) {
        handle_getall(s, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Peer", "Ping")) {
        DBusMessage *r = dbus_message_new_method_return(msg);
        if (r) { dbus_connection_send(conn, r, NULL); dbus_message_unref(r); }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult systray_filter(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    struct systray *s = ud;
    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name = NULL, *old = NULL, *new = NULL;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
                DBUS_TYPE_STRING, &old, DBUS_TYPE_STRING, &new,
                DBUS_TYPE_INVALID)) {
            if (name && new && !new[0]) remove_item(s, name);
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

struct systray *systray_create(struct wayland_ctx *ctx)
{
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn) {
        log_warn("systray: session bus unavailable: %s",
                 dbus_error_is_set(&err) ? err.message : "unknown");
        dbus_error_free(&err);
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(conn, FALSE);

    struct systray *s = calloc(1, sizeof(*s));
    if (!s) { dbus_connection_unref(conn); return NULL; }
    s->ctx = ctx;
    s->conn = conn;

    int r = dbus_bus_request_name(
        conn, "org.kde.StatusNotifierWatcher",
        DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    s->owns_watcher = (r == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    if (!s->owns_watcher) {
        log_warn("systray: another StatusNotifierWatcher is running: %s",
                 dbus_error_is_set(&err) ? err.message : "queued");
    } else {
        log_info("systray: registered org.kde.StatusNotifierWatcher");
    }
    dbus_error_free(&err);

    dbus_bus_add_match(conn,
        "type='method_call',interface='org.kde.StatusNotifierWatcher'", &err);
    dbus_bus_add_match(conn,
        "type='method_call',interface='org.freedesktop.DBus.Properties',"
        "path='/StatusNotifierWatcher'", &err);
    dbus_bus_add_match(conn,
        "type='signal',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged'", &err);
    dbus_error_free(&err);

    DBusObjectPathVTable vt;
    memset(&vt, 0, sizeof(vt));
    vt.message_function = watcher_method;
    if (!dbus_connection_register_object_path(conn, SNI_WATCHER_PATH, &vt, s)) {
        log_warn("systray: could not register watcher object path");
    }
    dbus_connection_add_filter(conn, systray_filter, s, NULL);
    return s;
}

void systray_destroy(struct systray *s)
{
    if (!s) return;
    for (int i = 0; i < s->n_items; i++)
        if (s->items[i].icon) cairo_surface_destroy(s->items[i].icon);
    if (s->conn) dbus_connection_unref(s->conn);
    free(s);
}

int systray_fd(struct systray *s)
{
    if (!s || !s->conn) return -1;
    int fd = -1;
    if (!dbus_connection_get_unix_fd(s->conn, &fd)) return -1;
    return fd;
}

bool systray_dispatch(struct systray *s)
{
    if (!s || !s->conn) return false;
    dbus_connection_read_write(s->conn, 0);
    while (dbus_connection_get_dispatch_status(s->conn) == DBUS_DISPATCH_DATA_REMAINS)
        dbus_connection_dispatch(s->conn);
    bool d = s->dirty;
    s->dirty = false;
    return d;
}

int systray_item_count(struct systray *s)
{
    return s ? s->n_items : 0;
}

double systray_width(struct systray *s)
{
    if (!s || s->n_items == 0) return 0.0;
    return s->n_items * 24.0;
}

void systray_draw(struct systray *s, cairo_t *cr, double x, int h)
{
    if (!s || s->n_items == 0) return;
    const double slot = 24.0;
    const double px = 18.0;
    for (int i = 0; i < s->n_items; i++) {
        double ix = x + i * slot;
        cairo_surface_t *icon = s->items[i].icon;
        if (icon) {
            cairo_set_source_surface(cr, icon, ix + (slot - px) / 2.0,
                                     (h - px) / 2.0);
            cairo_paint(cr);
        } else {
            /* no icon resolved: draw a dot so the item is still visible */
            cairo_set_source_rgba(cr, 0.8, 0.8, 1.0, 0.9);
            cairo_arc(cr, ix + slot / 2.0, h / 2.0, 3.0, 0, 2 * 3.14159265);
            cairo_fill(cr);
        }
    }
}
