#include "media/mpris.h"
#include "bar/backends.h"
#include "util/log.h"

#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

#define MPRIS_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_PLAYER_IFACE "org.mpris.MediaPlayer2.Player"
#define MPRIS_PATH "/org/mpris/MediaPlayer2"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_IFACE "org.freedesktop.DBus"
#define DBUS_PATH "/org/freedesktop/DBus"

struct mpris {
    DBusConnection *conn;
    struct backends_state *bk;
    char player[128];
    bool have_player;
    bool need_query;
    bool last_present;
    bool last_playing;
    char last_title[160];
    char last_artist[128];
    char last_player[64];
};

static bool name_is_player(const char *n)
{
    return n && strncmp(n, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) == 0 &&
           n[strlen(MPRIS_PREFIX)] != '\0';
}

static const char *short_name(const char *bus)
{
    return name_is_player(bus) ? bus + strlen(MPRIS_PREFIX) : bus;
}

static void publish(struct mpris *m)
{
    struct backends_state *b = m->bk;
    if (!b) return;
    b->media_present = m->have_player && b->media_title[0];
    snprintf(b->media_player, sizeof(b->media_player), "%s", m->have_player ? short_name(m->player) : "");
}

/* ---- metadata parsing --------------------------------------------------- */

static void read_metadata_array(struct mpris *m, DBusMessageIter *arr)
{
    struct backends_state *b = m->bk;
    b->media_title[0] = '\0';
    b->media_artist[0] = '\0';

    DBusMessageIter dict;
    dbus_message_iter_recurse(arr, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, var;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        if (key && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&entry, &var);
            if (strcmp(key, "xesam:title") == 0 &&
                dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *t = NULL;
                dbus_message_iter_get_basic(&var, &t);
                if (t) snprintf(b->media_title, sizeof(b->media_title), "%s", t);
            } else if (strcmp(key, "xesam:artist") == 0 &&
                       dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
                DBusMessageIter list;
                dbus_message_iter_recurse(&var, &list);
                if (dbus_message_iter_get_arg_type(&list) == DBUS_TYPE_STRING) {
                    const char *a = NULL;
                    dbus_message_iter_get_basic(&list, &a);
                    if (a) snprintf(b->media_artist, sizeof(b->media_artist), "%s", a);
                }
            }
        }
        dbus_message_iter_next(&dict);
    }
}

/* Iterate an a{sv} dict of changed properties. */
static void apply_changed(struct mpris *m, DBusMessageIter *arr)
{
    struct backends_state *b = m->bk;
    DBusMessageIter dict;
    dbus_message_iter_recurse(arr, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, var;
        dbus_message_iter_recurse(&dict, &entry);
        const char *key = NULL;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
            dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        if (key && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&entry, &var);
            if (strcmp(key, "Metadata") == 0 &&
                dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
                read_metadata_array(m, &var);
            } else if (strcmp(key, "PlaybackStatus") == 0 &&
                       dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *s = NULL;
                dbus_message_iter_get_basic(&var, &s);
                b->media_playing = s && strcmp(s, "Playing") == 0;
            }
        }
        dbus_message_iter_next(&dict);
    }
    publish(m);
}

/* ---- blocking property query -------------------------------------------- */

static DBusMessage *prop_get(struct mpris *m, const char *prop)
{
    DBusMessage *msg = dbus_message_new_method_call(m->player, MPRIS_PATH, PROPS_IFACE, "Get");
    if (!msg) return NULL;
    const char *iface = MPRIS_PLAYER_IFACE;
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m->conn, msg, 800, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return NULL;
    }
    return reply;
}

static void query_player(struct mpris *m)
{
    struct backends_state *b = m->bk;
    if (!m->have_player || !b) return;

    DBusMessage *reply = prop_get(m, "Metadata");
    if (reply) {
        DBusMessageIter args, var;
        if (dbus_message_iter_init(reply, &args) &&
            dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&args, &var);
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY)
                read_metadata_array(m, &var);
        }
        dbus_message_unref(reply);
    }
    reply = prop_get(m, "PlaybackStatus");
    if (reply) {
        DBusMessageIter args, var;
        if (dbus_message_iter_init(reply, &args) &&
            dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
            dbus_message_iter_recurse(&args, &var);
            if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                const char *s = NULL;
                dbus_message_iter_get_basic(&var, &s);
                b->media_playing = s && strcmp(s, "Playing") == 0;
            }
        }
        dbus_message_unref(reply);
    }
    publish(m);
}

/* Pick a player: prefer one currently Playing, else the first found. */
static void discover_player(struct mpris *m)
{
    DBusMessage *msg = dbus_message_new_method_call(DBUS_IFACE, DBUS_PATH, DBUS_IFACE, "ListNames");
    if (!msg) return;
    DBusError err;
    dbus_error_init(&err);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(m->conn, msg, 800, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); return; }

    char first[128] = "";
    char playing[128] = "";
    if (reply) {
        DBusMessageIter args, arr;
        if (dbus_message_iter_init(reply, &args) &&
            dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&args, &arr);
            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
                const char *name = NULL;
                dbus_message_iter_get_basic(&arr, &name);
                if (name_is_player(name)) {
                    if (!first[0]) snprintf(first, sizeof(first), "%s", name);
                    char save[128];
                    snprintf(save, sizeof(save), "%s", m->player);
                    snprintf(m->player, sizeof(m->player), "%s", name);
                    m->have_player = true;
                    DBusMessage *r = prop_get(m, "PlaybackStatus");
                    if (r) {
                        DBusMessageIter a, v;
                        if (dbus_message_iter_init(r, &a) &&
                            dbus_message_iter_get_arg_type(&a) == DBUS_TYPE_VARIANT) {
                            dbus_message_iter_recurse(&a, &v);
                            if (dbus_message_iter_get_arg_type(&v) == DBUS_TYPE_STRING) {
                                const char *s = NULL;
                                dbus_message_iter_get_basic(&v, &s);
                                if (s && strcmp(s, "Playing") == 0 && !playing[0])
                                    snprintf(playing, sizeof(playing), "%s", name);
                            }
                        }
                        dbus_message_unref(r);
                    }
                    snprintf(m->player, sizeof(m->player), "%s", save);
                }
                dbus_message_iter_next(&arr);
            }
        }
        dbus_message_unref(reply);
    }

    const char *chosen = playing[0] ? playing : first;
    if (chosen[0]) {
        snprintf(m->player, sizeof(m->player), "%s", chosen);
        m->have_player = true;
    } else {
        m->have_player = false;
        m->player[0] = '\0';
        if (m->bk) {
            m->bk->media_present = false;
            m->bk->media_title[0] = '\0';
            m->bk->media_artist[0] = '\0';
            m->bk->media_playing = false;
            m->bk->media_player[0] = '\0';
        }
    }
}

/* ---- D-Bus filter ------------------------------------------------------- */

static DBusHandlerResult mpris_filter(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    struct mpris *m = ud;

    if (dbus_message_is_signal(msg, DBUS_IFACE, "NameOwnerChanged")) {
        DBusMessageIter args;
        if (dbus_message_iter_init(msg, &args)) {
            const char *name = NULL, *old = NULL, *new = NULL;
            if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&args, &name);
            dbus_message_iter_next(&args);
            if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&args, &old);
            dbus_message_iter_next(&args);
            if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&args, &new);
            if (name && name_is_player(name)) {
                bool appeared = (!old || !old[0]) && new && new[0];
                bool vanished = (!new || !new[0]);
                if (vanished && m->have_player && strcmp(name, m->player) == 0) {
                    m->have_player = false;
                    m->need_query = true;
                } else if (appeared && !m->have_player) {
                    m->need_query = true;
                }
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_is_signal(msg, PROPS_IFACE, "PropertiesChanged")) {
        if (m->have_player && strcmp(dbus_message_get_sender(msg), m->player) == 0) {
            DBusMessageIter args;
            if (dbus_message_iter_init(msg, &args)) {
                const char *iface = NULL;
                if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
                    dbus_message_iter_get_basic(&args, &iface);
                dbus_message_iter_next(&args);
                if (iface && strcmp(iface, MPRIS_PLAYER_IFACE) == 0 &&
                    dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
                    apply_changed(m, &args);
            }
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---- lifecycle ---------------------------------------------------------- */

struct mpris *mpris_create(struct backends_state *bk)
{
    struct mpris *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->bk = bk;

    DBusError err;
    dbus_error_init(&err);
    m->conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!m->conn) {
        log_warn("mpris: no session bus (%s)", err.message ? err.message : "?");
        dbus_error_free(&err);
        free(m);
        return NULL;
    }
    dbus_connection_set_exit_on_disconnect(m->conn, FALSE);
    dbus_connection_add_filter(m->conn, mpris_filter, m, NULL);

    dbus_bus_add_match(m->conn,
                       "type='signal',interface='" DBUS_IFACE "',member='NameOwnerChanged',"
                       "arg0namespace='org.mpris.MediaPlayer2'", &err);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); }
    dbus_bus_add_match(m->conn,
                       "type='signal',interface='" PROPS_IFACE "',member='PropertiesChanged'", &err);
    if (dbus_error_is_set(&err)) { dbus_error_free(&err); }
    dbus_connection_flush(m->conn);

    discover_player(m);
    if (m->have_player) query_player(m);
    log_info("mpris: ready (player=%s)", m->have_player ? short_name(m->player) : "none");
    return m;
}

void mpris_destroy(struct mpris *m)
{
    if (!m) return;
    if (m->conn) {
        dbus_connection_remove_filter(m->conn, mpris_filter, m);
        dbus_connection_close(m->conn);
    }
    free(m);
}

int mpris_fd(struct mpris *m)
{
    if (!m || !m->conn) return -1;
    int fd = -1;
    if (!dbus_connection_get_unix_fd(m->conn, &fd)) return -1;
    return fd;
}

bool mpris_dispatch(struct mpris *m)
{
    if (!m || !m->conn) return false;
    dbus_connection_read_write(m->conn, 0);
    while (dbus_connection_dispatch(m->conn) == DBUS_DISPATCH_DATA_REMAINS)
        ;
    dbus_connection_flush(m->conn);

    if (m->need_query) {
        m->need_query = false;
        discover_player(m);
        query_player(m);
    }

    struct backends_state *b = m->bk;
    if (!b) return false;
    bool present = m->have_player && b->media_title[0];
    bool changed = present != m->last_present ||
                   b->media_playing != m->last_playing ||
                   strcmp(b->media_title, m->last_title) != 0 ||
                   strcmp(b->media_artist, m->last_artist) != 0 ||
                   strcmp(b->media_player, m->last_player) != 0;
    m->last_present = present;
    m->last_playing = b->media_playing;
    snprintf(m->last_title, sizeof(m->last_title), "%s", b->media_title);
    snprintf(m->last_artist, sizeof(m->last_artist), "%s", b->media_artist);
    snprintf(m->last_player, sizeof(m->last_player), "%s", b->media_player);
    return changed;
}
