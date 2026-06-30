/* src/ipc_dbus.c
 *
 * D-Bus back-end implementation of the ipc_backend.h interface.
 * Selected at build time with: make IPC_BACKEND=dbus  (default)
 *
 * A single, persistent DBusConnection is kept alive between
 * ipc_init() / ipc_cleanup() to avoid reconnecting on every signal.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus.h>

#include "ipc_backend.h"

#define OBJECT_PATH    "/com/example/LogWatcher"
#define INTERFACE_NAME "com.example.LogWatcher"
#define SIGNAL_NAME    "NewLog"

static DBusConnection *g_conn = NULL;

/* ----------------------------------------------------
 * ipc_init
 * ----------------------------------------------------
 */
int ipc_init(void) {
    DBusError err;
    dbus_error_init(&err);

    g_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus Connection Error: %s\n", err.message);
        dbus_error_free(&err);
        return -1;
    }
    if (g_conn == NULL) {
        fprintf(stderr, "DBus connection is NULL\n");
        return -1;
    }
    return 0;
}

/* ----------------------------------------------------
 * ipc_cleanup
 * ----------------------------------------------------
 */
void ipc_cleanup(void) {
    if (g_conn != NULL) {
        dbus_connection_unref(g_conn);
        g_conn = NULL;
    }
}

/* ----------------------------------------------------
 * ipc_send_signal
 * ----------------------------------------------------
 */
int ipc_send_signal(const char *message) {
    if (g_conn == NULL) {
        fprintf(stderr, "DBus not initialized\n");
        return -1;
    }

    DBusMessage *msg = dbus_message_new_signal(OBJECT_PATH, INTERFACE_NAME, SIGNAL_NAME);
    if (msg == NULL) {
        fprintf(stderr, "Failed to create DBus message\n");
        return -1;
    }

    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &message, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "Failed to append arguments to DBus message\n");
        dbus_message_unref(msg);
        return -1;
    }

    if (!dbus_connection_send(g_conn, msg, NULL)) {
        fprintf(stderr, "Failed to send DBus message: Out Of Memory\n");
        dbus_message_unref(msg);
        return -1;
    }

    dbus_connection_flush(g_conn);
    dbus_message_unref(msg);
    printf("Sent DBus signal with log: %s\n", message);
    return 0;
}

/* ----------------------------------------------------
 * ipc_listen
 * ----------------------------------------------------
 */
int ipc_listen(ipc_message_handler_t handler) {
    if (g_conn == NULL) {
        fprintf(stderr, "DBus not initialized\n");
        return -1;
    }

    DBusError err;
    dbus_error_init(&err);

    char match_rule[256];
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='%s',member='%s'",
             INTERFACE_NAME, SIGNAL_NAME);

    dbus_bus_add_match(g_conn, match_rule, &err);
    dbus_connection_flush(g_conn);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus Add Match Error: %s\n", err.message);
        dbus_error_free(&err);
        return -1;
    }

    printf("Listening for D-Bus signals...\n");

    while (1) {
        dbus_connection_read_write(g_conn, 0);
        DBusMessage *msg = dbus_connection_pop_message(g_conn);
        if (msg == NULL) {
            usleep(100000); /* 100 ms */
            continue;
        }

        if (dbus_message_is_signal(msg, INTERFACE_NAME, SIGNAL_NAME)) {
            const char *received_msg;
            if (dbus_message_get_args(msg, &err,
                                      DBUS_TYPE_STRING, &received_msg,
                                      DBUS_TYPE_INVALID)) {
                handler(received_msg);
            } else {
                fprintf(stderr, "Failed to get message arguments: %s\n", err.message);
                dbus_error_free(&err);
            }
        }
        dbus_message_unref(msg);
    }
    return 0; /* unreachable */
}
