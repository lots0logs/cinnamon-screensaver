#include "cs-notification-watcher.h"

enum {
        NOTIFICATION_RECEIVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (CsNotificationWatcher, cs_notification_watcher, G_TYPE_OBJECT);

#define DBUS_SERVICE "org.freedesktop.DBus"
#define DBUS_PATH "/org/freedesktop/DBus"
#define DBUS_INTERFACE "org.freedesktop.DBus"
#define MATCH_RULE "type='method_call', interface='org.freedesktop.Notifications', member='Notify', eavesdrop=true"

#define NOTIFICATIONS_INTERFACE "org.freedesktop.Notifications"
#define NOTIFY_METHOD "Notify"

gboolean
idle_notify_received (gpointer user_data)
{
    g_return_val_if_fail (CS_IS_NOTIFICATION_WATCHER (user_data), FALSE);

    CsNotificationWatcher *watcher = CS_NOTIFICATION_WATCHER (user_data);

    g_signal_emit (watcher, signals[NOTIFICATION_RECEIVED], 0);

    return FALSE;
}

GDBusMessage *
notification_filter_func (GDBusConnection *connection,
                          GDBusMessage    *message,
                          gboolean        *incoming,
                          gpointer         user_data)
{
    const gchar *dest;

    CsNotificationWatcher *watcher = CS_NOTIFICATION_WATCHER (user_data);

    if (incoming &&
        g_dbus_message_get_message_type (message) == G_DBUS_MESSAGE_TYPE_METHOD_CALL &&
        g_strcmp0 (g_dbus_message_get_interface (message), NOTIFICATIONS_INTERFACE) == 0 &&
        g_strcmp0 (g_dbus_message_get_member (message), NOTIFY_METHOD) == 0) {
        g_idle_add (idle_notify_received, watcher);
    }

    dest = g_dbus_message_get_destination (message);

    if (!incoming || g_strcmp0 (dest, g_dbus_connection_get_unique_name (connection)) == 0) {
        return message;
    } else {
        g_clear_object (&message);
        return NULL;
    }
}

static void
cs_notification_watcher_init (CsNotificationWatcher *watcher)
{
    GError *error = NULL;
    GVariant *rulev;
    GVariant *result;

    watcher->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

    if (!watcher->connection) {
        g_printerr ("CsNotificationWatcher: Could not connect to session bus - %s\n", error->message);
        g_clear_error (&error);
        return;
    }

    rulev = g_variant_new("(s)", MATCH_RULE);

    result = g_dbus_connection_call_sync (watcher->connection,
                                          DBUS_SERVICE,
                                          DBUS_PATH,
                                          DBUS_INTERFACE,
                                          "AddMatch",
                                          rulev,
                                          G_VARIANT_TYPE ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          &error);

    if (!result) {
        g_printerr ("CsNotificationWatcher: Could not add match rule to bus - %s\n", error->message);
        g_clear_error (&error);
        return;
    }

    watcher->filter_id = g_dbus_connection_add_filter (watcher->connection,
                                                       (GDBusMessageFilterFunction) notification_filter_func,
                                                       watcher,
                                                       NULL);
}


static void
cs_notification_watcher_dispose (GObject *object)
{
    CsNotificationWatcher *watcher;

    g_return_if_fail (object != NULL);
    g_return_if_fail (CS_IS_NOTIFICATION_WATCHER (object));

    watcher = CS_NOTIFICATION_WATCHER (object);

    if (watcher->filter_id > 0) {
        g_dbus_connection_remove_filter (watcher->connection, watcher->filter_id);
        watcher->filter_id = 0;
    }

    g_clear_object (&watcher->connection);

    G_OBJECT_CLASS (cs_notification_watcher_parent_class)->dispose (object);
}

static void
cs_notification_watcher_class_init (CsNotificationWatcherClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose     = cs_notification_watcher_dispose;

    signals [NOTIFICATION_RECEIVED] =
        g_signal_new ("notification-received",
        G_TYPE_FROM_CLASS (object_class),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET (CsNotificationWatcherClass, notification_received),
        NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);
}

CsNotificationWatcher *
cs_notification_watcher_new (void)
{
    GObject     *result;

    result = g_object_new (CS_TYPE_NOTIFICATION_WATCHER,
                           NULL);

    return CS_NOTIFICATION_WATCHER (result);
}
