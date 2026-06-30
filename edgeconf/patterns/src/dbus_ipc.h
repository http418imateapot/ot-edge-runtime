/* src/dbus_ipc.h
 *
 * D-Bus IPC helpers for the config sync daemon.
 *
 * Signal payload format (JSON string, interface_version for compatibility):
 *
 *   {"interface_version":1,"key":"sample_rate","value":"120"}
 *
 * The special key "__snapshot__" carries the full config content and is sent
 * once when watch mode starts so late-joining dashboards can initialise.
 */
#ifndef DBUS_IPC_H
#define DBUS_IPC_H

#include <dbus/dbus.h>

/* D-Bus addressing */
#define DBUS_OBJECT_PATH   "/com/example/RobustConfig"
#define DBUS_IFACE_NAME    "com.example.RobustConfig"
#define DBUS_SIGNAL_NAME   "ConfigChanged"

/* Bump this when the payload schema changes */
#define DBUS_IFACE_VERSION 1

typedef enum {
    BUS_SESSION = 0,
    BUS_SYSTEM  = 1,
} bus_type_t;

/* Connect to the chosen D-Bus bus.
 * Returns a shared connection pointer, or NULL on error. */
DBusConnection *dbus_ipc_connect(bus_type_t bus_type);

/* Send a ConfigChanged signal carrying a JSON payload with key/value.
 * Returns 0 on success, -1 on error. */
int dbus_ipc_send(DBusConnection *conn,
                  const char *key, const char *value);

/* Run the dashboard loop: subscribe and print every ConfigChanged signal.
 * Blocks until the process is killed. */
int dbus_ipc_dashboard(DBusConnection *conn);

#endif /* DBUS_IPC_H */
