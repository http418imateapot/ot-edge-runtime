/* src/dbus_ipc.c */
#include "dbus_ipc.h"
#include "logger.h"
#include "config_io.h"   /* CONFIG_MAX_VALUE */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * dbus_ipc_connect
 * -------------------------------------------------------------------------*/
DBusConnection *dbus_ipc_connect(bus_type_t bus_type) {
    DBusError err;
    dbus_error_init(&err);

    DBusBusType bt = (bus_type == BUS_SYSTEM) ? DBUS_BUS_SYSTEM
                                              : DBUS_BUS_SESSION;
    DBusConnection *conn = dbus_bus_get(bt, &err);

    if (dbus_error_is_set(&err)) {
        log_err("D-Bus connect (%s bus) failed: %s",
                (bus_type == BUS_SYSTEM) ? "system" : "session",
                err.message);
        dbus_error_free(&err);
        return NULL;
    }
    if (!conn) {
        log_err("D-Bus connect returned NULL");
        return NULL;
    }

    log_debug("Connected to D-Bus %s bus",
              (bus_type == BUS_SYSTEM) ? "system" : "session");
    return conn;
}

/* ---------------------------------------------------------------------------
 * dbus_ipc_send
 * -------------------------------------------------------------------------*/
int dbus_ipc_send(DBusConnection *conn,
                  const char *key, const char *value) {
    char payload[CONFIG_MAX_VALUE * 2 + 128];  /* generous headroom */
    snprintf(payload, sizeof(payload),
             "{\"interface_version\":%d,\"key\":\"%s\",\"value\":\"%s\"}",
             DBUS_IFACE_VERSION, key, value);

    DBusMessage *msg = dbus_message_new_signal(DBUS_OBJECT_PATH,
                                               DBUS_IFACE_NAME,
                                               DBUS_SIGNAL_NAME);
    if (!msg) {
        log_err("dbus_ipc_send: failed to allocate signal message");
        return -1;
    }

    const char *p = payload;
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_STRING, &p,
                                  DBUS_TYPE_INVALID)) {
        log_err("dbus_ipc_send: failed to append payload");
        dbus_message_unref(msg);
        return -1;
    }

    if (!dbus_connection_send(conn, msg, NULL)) {
        log_err("dbus_ipc_send: send failed (out of memory)");
        dbus_message_unref(msg);
        return -1;
    }

    dbus_connection_flush(conn);
    dbus_message_unref(msg);

    log_debug("D-Bus signal sent: key=%s value=%s", key, value);
    return 0;
}

/* ---------------------------------------------------------------------------
 * dbus_ipc_dashboard
 * -------------------------------------------------------------------------*/
int dbus_ipc_dashboard(DBusConnection *conn) {
    DBusError err;
    dbus_error_init(&err);

    char match_rule[512];
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='%s',member='%s'",
             DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);

    dbus_bus_add_match(conn, match_rule, &err);
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        log_err("dbus_ipc_dashboard: add_match failed: %s", err.message);
        dbus_error_free(&err);
        return -1;
    }

    log_info("Dashboard listening on %s.%s ...",
             DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);

    while (1) {
        dbus_connection_read_write(conn, 0);
        DBusMessage *msg = dbus_connection_pop_message(conn);
        if (!msg) {
            usleep(100000);   /* 100 ms idle poll */
            continue;
        }

        if (dbus_message_is_signal(msg, DBUS_IFACE_NAME, DBUS_SIGNAL_NAME)) {
            const char *payload = NULL;
            if (dbus_message_get_args(msg, &err,
                                      DBUS_TYPE_STRING, &payload,
                                      DBUS_TYPE_INVALID)) {
                printf("ConfigChanged: %s\n", payload);
                fflush(stdout);
            } else {
                log_warn("dbus_ipc_dashboard: cannot parse args: %s",
                         err.message);
                dbus_error_free(&err);
            }
        }

        dbus_message_unref(msg);
    }

    return 0;   /* unreachable unless loop is broken */
}
