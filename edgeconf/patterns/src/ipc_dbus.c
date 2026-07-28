/* src/ipc_dbus.c
 *
 * D-Bus adapter for the ipc_backend.h port — the reference implementation.
 *
 * Selected at build time by the default:  make            (IPC_BACKEND=dbus)
 * Requires libdbus-1.  Debian/Ubuntu:     apt-get install libdbus-1-dev
 *
 * Wire format
 * -----------
 * Each delta is broadcast as a D-Bus signal carrying a single string argument:
 *
 *   {"interface_version":2,"key":"sample_rate","value":"120","deleted":false}
 *
 * `deleted` distinguishes "this key is gone" from "this key is now the empty
 * string". Version 1 had no such field and no way to express a removal.
 *
 * The adapter owns this encoding; the daemon above the port never sees it.
 * `interface_version` is bumped whenever the payload schema changes, so a
 * subscriber built against an older schema can detect the mismatch instead of
 * silently misreading fields.
 *
 * Addressing
 * ----------
 * The transport-neutral `address` string is either "system" or "session",
 * naming the D-Bus bus to attach to. System bus is the default: it is the one
 * that exists on a headless device under systemd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>

#include "ipc_backend.h"
#include "logger.h"
#include "config_io.h"   /* CONFIG_MAX_KEY, CONFIG_MAX_VALUE */

/* -----------------------------------------------------------------
 * D-Bus addressing — private to this adapter
 * ----------------------------------------------------------------*/
#define DBUS_OBJECT_PATH   "/com/example/RobustConfig"
#define DBUS_IFACE_NAME    "com.example.RobustConfig"
#define DBUS_SIGNAL_NAME   "ConfigChanged"
#define DBUS_IFACE_VERSION 2

/* Worst case each byte expands to \u00XX (6 chars), plus JSON scaffolding
 * and a comfortable margin. */
#define MAX_PAYLOAD ((CONFIG_MAX_KEY + CONFIG_MAX_VALUE) * 6 + 128)

/* Blocking read timeout, milliseconds. Bounds how long run() takes to notice
 * process termination; it is not a polling interval. */
#define READ_TIMEOUT_MS 500

typedef struct {
    DBusConnection *conn;
    ipc_event_cb    cb;
    void           *user;
    int             subscribed;
    int             owns_name;
} dbus_state_t;

/* -----------------------------------------------------------------
 * Minimal JSON string escaping / unescaping
 *
 * Only what this adapter's own payload needs: it produces the JSON and it
 * consumes the JSON, so the two halves stay in step. A key or value containing
 * a quote or a backslash has to survive the round trip — without escaping,
 * such a value produced malformed JSON that a subscriber could not parse.
 * ----------------------------------------------------------------*/
static int json_escape(const char *src, char *dst, size_t dst_size) {
    size_t o = 0;

    for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
        char        tmp[7];
        const char *seq;
        size_t      seq_len;

        switch (*p) {
        case '"':  seq = "\\\""; seq_len = 2; break;
        case '\\': seq = "\\\\"; seq_len = 2; break;
        case '\n': seq = "\\n";  seq_len = 2; break;
        case '\r': seq = "\\r";  seq_len = 2; break;
        case '\t': seq = "\\t";  seq_len = 2; break;
        default:
            if (*p < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
                seq = tmp; seq_len = 6;
            } else {
                tmp[0] = (char)*p; tmp[1] = '\0';
                seq = tmp; seq_len = 1;
            }
        }

        if (o + seq_len >= dst_size)
            return -1;                   /* caller reports truncation */
        memcpy(dst + o, seq, seq_len);
        o += seq_len;
    }

    if (o >= dst_size)
        return -1;
    dst[o] = '\0';
    return 0;
}

/* Read the JSON string value of "<field>" out of `json` into `out`.
 * Returns 0 on success, -1 if the field is absent or does not fit. */
static int json_extract(const char *json, const char *field,
                        char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", field);

    const char *p = strstr(json, pattern);
    if (p == NULL)
        return -1;
    p += strlen(pattern);

    size_t o = 0;
    while (*p != '\0' && *p != '"') {
        char c;

        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'u': {
                char hex[5];
                if (strlen(p + 1) < 4)
                    return -1;
                memcpy(hex, p + 1, 4);
                hex[4] = '\0';
                c = (char)strtol(hex, NULL, 16);
                p += 4;
                break;
            }
            case '\0': return -1;        /* trailing backslash */
            default:   c = *p;  break;
            }
            p++;
        } else {
            c = *p++;
        }

        if (o + 1 >= out_size)
            return -1;
        out[o++] = c;
    }

    if (*p != '"')
        return -1;                       /* unterminated string */

    out[o] = '\0';
    return 0;
}

/* -----------------------------------------------------------------
 * open / close
 * ----------------------------------------------------------------*/
static ipc_status_t dbus_open(ipc_channel_t *ch, const char *address) {
    DBusBusType bus;

    if (address == NULL || strcmp(address, "system") == 0) {
        bus     = DBUS_BUS_SYSTEM;
        address = "system";
    } else if (strcmp(address, "session") == 0) {
        bus = DBUS_BUS_SESSION;
    } else {
        log_err("dbus: unknown address '%s'; expected 'system' or 'session'",
                address);
        return IPC_ERR_ADDRESS;
    }

    dbus_state_t *st = calloc(1, sizeof(*st));
    if (st == NULL) {
        log_err("dbus: out of memory");
        return IPC_ERR_CONNECT;
    }

    DBusError err;
    dbus_error_init(&err);

    st->conn = dbus_bus_get(bus, &err);
    if (dbus_error_is_set(&err)) {
        log_err("dbus: connect to %s bus failed: %s", address, err.message);
        dbus_error_free(&err);
        free(st);
        return IPC_ERR_CONNECT;
    }
    if (st->conn == NULL) {
        log_err("dbus: connect to %s bus returned NULL", address);
        free(st);
        return IPC_ERR_CONNECT;
    }

    /* Losing the bus should not take the daemon down: watch mode keeps
     * serving the file even if the bus goes away. */
    dbus_connection_set_exit_on_disconnect(st->conn, FALSE);

    ch->state = st;
    log_debug("dbus: connected to %s bus", address);
    return IPC_OK;
}

static void dbus_close(ipc_channel_t *ch) {
    dbus_state_t *st = ch->state;
    if (st == NULL)
        return;

    /* dbus_bus_get() hands out a shared connection; unref rather than close,
     * so any other user in the process is unaffected. */
    if (st->conn != NULL)
        dbus_connection_unref(st->conn);

    free(st);
    ch->state = NULL;
}

/* -----------------------------------------------------------------
 * publish
 * ----------------------------------------------------------------*/
/* Claim the well-known bus name. Done on first publish rather than in open(),
 * because a subscriber opens a channel too and must not contend for the name.
 * Owning it is what lets subscribers filter on sender= and reject forged
 * ConfigChanged signals from any other process on the bus. */
static ipc_status_t dbus_acquire_name(dbus_state_t *st) {
    if (st->owns_name)
        return IPC_OK;

    DBusError err;
    dbus_error_init(&err);

    int rc = dbus_bus_request_name(st->conn, DBUS_IFACE_NAME,
                                   DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (dbus_error_is_set(&err)) {
        log_err("dbus: requesting name %s failed: %s",
                DBUS_IFACE_NAME, err.message);
        dbus_error_free(&err);
        return IPC_ERR_PUBLISH;
    }
    if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        log_err("dbus: %s is already owned by another process; refusing to publish", DBUS_IFACE_NAME);
        return IPC_ERR_PUBLISH;
    }

    st->owns_name = 1;
    log_debug("dbus: owns %s", DBUS_IFACE_NAME);
    return IPC_OK;
}

static ipc_status_t dbus_publish(ipc_channel_t *ch, const ipc_kv_t *kv) {
    dbus_state_t *st = ch->state;
    if (st == NULL || st->conn == NULL)
        return IPC_ERR_STATE;

    ipc_status_t nst = dbus_acquire_name(st);
    if (nst != IPC_OK)
        return nst;

    char esc_key[CONFIG_MAX_KEY * 6 + 1];
    char esc_val[CONFIG_MAX_VALUE * 6 + 1];

    if (json_escape(kv->key, esc_key, sizeof(esc_key)) != 0 ||
        json_escape(kv->value, esc_val, sizeof(esc_val)) != 0) {
        log_err("dbus: key or value too long to encode; delta dropped");
        return IPC_ERR_PUBLISH;
    }

    char payload[MAX_PAYLOAD];
    int n = snprintf(payload, sizeof(payload),
                     "{\"interface_version\":%d,\"key\":\"%s\","
                     "\"value\":\"%s\",\"deleted\":%s}",
                     DBUS_IFACE_VERSION, esc_key, esc_val,
                     kv->deleted ? "true" : "false");
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        log_err("dbus: payload truncated; delta dropped");
        return IPC_ERR_PUBLISH;
    }

    DBusMessage *msg = dbus_message_new_signal(DBUS_OBJECT_PATH,
                                               DBUS_IFACE_NAME,
                                               DBUS_SIGNAL_NAME);
    if (msg == NULL) {
        log_err("dbus: cannot allocate signal message");
        return IPC_ERR_PUBLISH;
    }

    const char  *p  = payload;
    ipc_status_t rc = IPC_OK;

    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &p, DBUS_TYPE_INVALID)) {
        log_err("dbus: cannot append payload");
        rc = IPC_ERR_PUBLISH;
    } else if (!dbus_connection_send(st->conn, msg, NULL)) {
        log_err("dbus: send failed (out of memory)");
        rc = IPC_ERR_PUBLISH;
    } else {
        dbus_connection_flush(st->conn);
        log_debug("dbus: published key=%s%s", kv->key,
                  kv->deleted ? " (removed)" : "");
    }

    dbus_message_unref(msg);
    return rc;
}

/* -----------------------------------------------------------------
 * subscribe / run
 * ----------------------------------------------------------------*/
static ipc_status_t dbus_subscribe(ipc_channel_t *ch, ipc_event_cb cb, void *user) {
    dbus_state_t *st = ch->state;
    if (st == NULL || st->conn == NULL)
        return IPC_ERR_STATE;

    /* sender= is the part that matters for trust: without it the bus delivers a
     * ConfigChanged from *any* process, and a local unprivileged process can
     * forge config changes that every subscriber accepts as genuine. */
    char rule[256];
    snprintf(rule, sizeof(rule),
             "type='signal',sender='%s',interface='%s',member='%s'",
             DBUS_IFACE_NAME, DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);

    DBusError err;
    dbus_error_init(&err);
    dbus_bus_add_match(st->conn, rule, &err);
    dbus_connection_flush(st->conn);

    if (dbus_error_is_set(&err)) {
        log_err("dbus: add_match failed: %s", err.message);
        dbus_error_free(&err);
        return IPC_ERR_SUBSCRIBE;
    }

    st->cb         = cb;
    st->user       = user;
    st->subscribed = 1;

    log_debug("dbus: subscribed to %s.%s", DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);
    return IPC_OK;
}

static ipc_status_t dbus_run(ipc_channel_t *ch) {
    dbus_state_t *st = ch->state;
    if (st == NULL || st->conn == NULL)
        return IPC_ERR_STATE;
    if (!st->subscribed) {
        log_err("dbus: run() called before subscribe()");
        return IPC_ERR_STATE;
    }

    log_info("Listening on %s.%s", DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);

    for (;;) {
        /* Blocks up to READ_TIMEOUT_MS waiting for traffic. */
        if (!dbus_connection_read_write(st->conn, READ_TIMEOUT_MS)) {
            log_warn("dbus: connection closed");
            return IPC_ERR_CONNECT;
        }

        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(st->conn)) != NULL) {
            if (!dbus_message_is_signal(msg, DBUS_IFACE_NAME, DBUS_SIGNAL_NAME)) {
                dbus_message_unref(msg);
                continue;
            }

            DBusError err;
            dbus_error_init(&err);

            const char *payload = NULL;
            if (!dbus_message_get_args(msg, &err,
                                       DBUS_TYPE_STRING, &payload,
                                       DBUS_TYPE_INVALID)) {
                log_warn("dbus: cannot read signal args: %s", err.message);
                dbus_error_free(&err);
                dbus_message_unref(msg);
                continue;
            }

            char key[CONFIG_MAX_KEY];
            char value[CONFIG_MAX_VALUE];

            if (json_extract(payload, "key", key, sizeof(key)) != 0 ||
                json_extract(payload, "value", value, sizeof(value)) != 0) {
                log_warn("dbus: unparseable payload, ignored: %s", payload);
                dbus_message_unref(msg);
                continue;
            }

            /* `deleted` is a JSON boolean, not a string, so it is matched
             * directly rather than through json_extract(). A v1 payload has no
             * such field and reads as not-deleted, which is the old meaning. */
            int deleted = (strstr(payload, "\"deleted\":true") != NULL);

            if (st->cb != NULL) {
                ipc_kv_t kv = { .key = key, .value = value,
                                .deleted = deleted };
                st->cb(&kv, st->user);
            }

            dbus_message_unref(msg);
        }
    }
}

/* -----------------------------------------------------------------
 * The adapter
 * ----------------------------------------------------------------*/
static const ipc_transport_t dbus_transport = {
    .name            = "dbus",
    .address_syntax  = "system | session",
    .default_address = "system",
    .open            = dbus_open,
    .close           = dbus_close,
    .publish         = dbus_publish,
    .subscribe       = dbus_subscribe,
    .run             = dbus_run,
};

const ipc_transport_t *ipc_transport(void) {
    return &dbus_transport;
}
