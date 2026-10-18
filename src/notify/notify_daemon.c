#include "notify/notify_daemon.h"
#include "notify/notification.h"
#include "notify/toast.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dbus/dbus.h>

#define NOTIFY_PATH "/org/freedesktop/Notifications"
#define NOTIFY_IFACE "org.freedesktop.Notifications"
#define NOTIFY_DEFAULT_TIMEOUT 5000
#define NOTIFY_PERSIST_TIMEOUT (24 * 60 * 60 * 1000)

struct notify_daemon {
    struct wayland_ctx *ctx;
    struct notification_store store;
    struct toast_panel *toast;
    DBusConnection *conn;
    bool running;
    bool name_acquired;
};

static dbus_bool_t send_signal(struct notify_daemon *nd, const char *member,
                               int first_type, unsigned int id, int second_type,
                               unsigned int reason)
{
    if (!nd->conn) return FALSE;
    DBusMessage *sig = dbus_message_new_signal(NOTIFY_PATH, NOTIFY_IFACE, member);
    if (!sig) return FALSE;
    dbus_message_append_args(sig,
                             first_type, &id,
                             second_type, &reason,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(nd->conn, sig, NULL);
    dbus_connection_flush(nd->conn);
    dbus_message_unref(sig);
    return TRUE;
}

static void on_toast_done(void *userdata, unsigned int id, unsigned int reason)
{
    struct notify_daemon *nd = userdata;
    send_signal(nd, "NotificationClosed", DBUS_TYPE_UINT32, id,
                DBUS_TYPE_UINT32, reason);
}

/* options: a{sv} hints -> urgency (byte) */
static int hint_urgency(DBusMessageIter *hints)
{
    DBusMessageIter dict;
    if (dbus_message_iter_get_arg_type(hints) != DBUS_TYPE_ARRAY) return -1;
    dbus_message_iter_recurse(hints, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, var;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&entry, &key);
            dbus_message_iter_next(&entry);
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                dbus_message_iter_recurse(&entry, &var);
                if (key && strcmp(key, "urgency") == 0 &&
                    dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BYTE) {
                    unsigned char u = 0;
                    dbus_message_iter_get_basic(&var, &u);
                    return (int)u;
                }
            }
        }
        dbus_message_iter_next(&dict);
    }
    return -1;
}

static DBusHandlerResult handle_notify(struct notify_daemon *nd, DBusMessage *msg)
{
    DBusMessageIter args;
    if (!dbus_message_iter_init(msg, &args)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    struct notification n;
    memset(&n, 0, sizeof(n));
    const char *app_name = "", *app_icon = "", *summary = "", *body = "";
    dbus_uint32_t replaces = 0;
    dbus_int32_t timeout = -1;
    int urgency = -1;
    bool has_actions = false;

    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&args, &app_name);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&args, &replaces);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&args, &app_icon);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&args, &summary);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
        dbus_message_iter_get_basic(&args, &body);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&args, &arr);
        has_actions = dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID;
    }
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
        urgency = hint_urgency(&args);
    dbus_message_iter_next(&args);
    if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_INT32)
        dbus_message_iter_get_basic(&args, &timeout);

    (void)has_actions;
    snprintf(n.app_name, sizeof(n.app_name), "%s", app_name ? app_name : "");
    snprintf(n.summary, sizeof(n.summary), "%s", summary ? summary : "");
    snprintf(n.body, sizeof(n.body), "%s", body ? body : "");
    snprintf(n.icon, sizeof(n.icon), "%s", app_icon ? app_icon : "");
    n.urgency = urgency < 0 ? 1 : urgency;
    n.timeout = timeout;

    unsigned int id = notify_store_add(&nd->store, &n);
    log_info("notification #%u from \"%s\": %s", id, n.app_name, n.summary);

    if (nd->toast) {
        int tmo = timeout;
        if (tmo == 0) tmo = NOTIFY_PERSIST_TIMEOUT;
        else if (tmo < 0) tmo = NOTIFY_DEFAULT_TIMEOUT;
        toast_show(nd->toast, &nd->store.items[nd->store.count - 1], tmo);
    }

    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
    dbus_connection_send(nd->conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_close(struct notify_daemon *nd, DBusMessage *msg)
{
    DBusMessageIter args;
    dbus_uint32_t id = 0;
    if (dbus_message_iter_init(msg, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&args, &id);

    for (int i = 0; i < nd->store.count; i++) {
        if (nd->store.items[i].id == id) {
            nd->store.items[i].read = 1;
            break;
        }
    }
    send_signal(nd, "NotificationClosed", DBUS_TYPE_UINT32, id,
                DBUS_TYPE_UINT32, 3u);
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_connection_send(nd->conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_caps(struct notify_daemon *nd, DBusMessage *msg)
{
    const char *caps[] = { "body", "body-markup", "actions", "icon-static",
                           "persistence", "sound", NULL };
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter it, arr;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    for (int i = 0; caps[i]; i++)
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &caps[i]);
    dbus_message_iter_close_container(&it, &arr);
    dbus_connection_send(nd->conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_server_info(struct notify_daemon *nd, DBusMessage *msg)
{
    const char *name = "Emancipation Shell";
    const char *vendor = "Emancipation OS";
    const char *version = "0.1.0";
    const char *spec = "1.2";
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_STRING, &name,
                             DBUS_TYPE_STRING, &vendor,
                             DBUS_TYPE_STRING, &version,
                             DBUS_TYPE_STRING, &spec,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(nd->conn, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult notify_filter(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    struct notify_daemon *nd = ud;
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL) {
        /* still handle the NameAcquired/NameLost signals if useful */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    const char *iface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    if (!member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (iface && strcmp(iface, NOTIFY_IFACE) != 0 &&
        strcmp(iface, "org.freedesktop.DBus.Properties") != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (strcmp(member, "Notify") == 0) return handle_notify(nd, msg);
    if (strcmp(member, "CloseNotification") == 0) return handle_close(nd, msg);
    if (strcmp(member, "GetCapabilities") == 0) return handle_get_caps(nd, msg);
    if (strcmp(member, "GetServerInformation") == 0) return handle_server_info(nd, msg);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

struct notify_daemon *notify_daemon_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct notify_daemon *nd = calloc(1, sizeof(*nd));
    if (!nd) return NULL;
    nd->ctx = ctx;
    notify_store_init(&nd->store);
    nd->running = true;

    DBusError err;
    dbus_error_init(&err);
    nd->conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!nd->conn) {
        log_warn("notification daemon: no session bus (%s)", err.message ? err.message : "?");
        dbus_error_free(&err);
        return nd; /* still usable, just no bus */
    }
    dbus_connection_set_exit_on_disconnect(nd->conn, FALSE);

    int ret = dbus_bus_request_name(nd->conn, NOTIFY_IFACE,
                                    DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        log_warn("notification daemon: RequestName: %s", err.message);
        dbus_error_free(&err);
    }
    nd->name_acquired = (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    if (nd->name_acquired)
        log_info("notification daemon: owns %s", NOTIFY_IFACE);
    else
        log_warn("notification daemon: %s already owned", NOTIFY_IFACE);

    dbus_connection_add_filter(nd->conn, notify_filter, nd, NULL);
    log_info("notification daemon ready");
    return nd;
}

void notify_daemon_destroy(struct notify_daemon *nd)
{
    if (!nd) return;
    if (nd->conn) {
        dbus_connection_remove_filter(nd->conn, notify_filter, nd);
        if (nd->name_acquired)
            dbus_bus_release_name(nd->conn, NOTIFY_IFACE, NULL);
    }
    notify_store_free(&nd->store);
    free(nd);
}

bool notify_daemon_is_running(struct notify_daemon *nd)
{
    return nd && nd->running;
}

struct notification_store *notify_daemon_store(struct notify_daemon *nd)
{
    if (!nd) return NULL;
    return &nd->store;
}

void notify_daemon_set_toast(struct notify_daemon *nd, struct toast_panel *t)
{
    if (!nd) return;
    nd->toast = t;
    if (t) toast_set_done_cb(t, on_toast_done, nd);
}

int notify_daemon_fd(struct notify_daemon *nd)
{
    if (!nd || !nd->conn) return -1;
    int fd = -1;
    if (!dbus_connection_get_unix_fd(nd->conn, &fd)) return -1;
    return fd;
}

bool notify_daemon_dispatch(struct notify_daemon *nd)
{
    if (!nd || !nd->conn) return false;
    dbus_connection_read_write(nd->conn, 0);
    bool did = false;
    while (dbus_connection_dispatch(nd->conn) == DBUS_DISPATCH_DATA_REMAINS)
        did = true;
    /* method replies queued during dispatch must be pushed out */
    dbus_connection_flush(nd->conn);
    return did;
}
