/* include/ipc_backend.h
 *
 * Transport-neutral interface for broadcasting configuration deltas.
 *
 * ---------------------------------------------------------------------------
 * Why this layer exists
 * ---------------------------------------------------------------------------
 * A config-sync daemon has to tell other processes that a key changed. *How*
 * it tells them is entirely a property of the platform:
 *
 *   - a systemd/D-Bus distribution (Debian, Yocto or Buildroot on a Jetson or
 *     a Raspberry Pi) already has a message bus running;
 *   - an OpenWrt / uClinux-class device has ubus instead, and no D-Bus at all;
 *   - a minimal or containerised image may have neither, and a plain Unix
 *     domain socket is the only thing available.
 *
 * None of that should leak into the daemon's logic. This header is the port;
 * each transport is an adapter behind it (the strategy pattern, selected at
 * build time so the binary links exactly one implementation and pulls in
 * exactly one set of libraries).
 *
 * The daemon speaks only in terms of key/value deltas. It never sees a
 * DBusConnection, a ubus_context or a socket fd.
 *
 * ---------------------------------------------------------------------------
 * What an adapter must provide
 * ---------------------------------------------------------------------------
 * Implement every ipc_transport_t function pointer in a file named
 * src/ipc_<name>.c, expose it through ipc_transport(), and add <name> to the
 * IPC_BACKEND options in the Makefile. The contract is:
 *
 *   open       Connect to the endpoint named by `address`. `address` is
 *              transport-specific and may be NULL, in which case the adapter
 *              uses its default_address. Returns IPC_OK or an error code, and
 *              must leave the channel safe to close() either way.
 *   close      Release everything open() acquired. Idempotent, and safe on a
 *              channel that never opened successfully.
 *   publish    Broadcast one key/value delta. The adapter owns the wire
 *              encoding — a subscriber of the same adapter must recover the
 *              key and value byte-for-byte.
 *   subscribe  Record the callback to invoke for each delta received.
 *              Does not block.
 *   run        Block, dispatching received deltas to the subscribed callback,
 *              until the process is terminated. Only meaningful after
 *              subscribe().
 *
 * Adapters must not write to stdout; presentation belongs to the caller.
 * Diagnostics go through logger.h.
 *
 * Currently implemented: dbus (src/ipc_dbus.c, the reference adapter).
 */

#ifndef IPC_BACKEND_H
#define IPC_BACKEND_H

/* -----------------------------------------------------------------
 * Status codes
 * ----------------------------------------------------------------*/
typedef enum {
    IPC_OK              =  0,
    IPC_ERR_CONNECT     = -1,   /* could not reach the endpoint           */
    IPC_ERR_PUBLISH     = -2,   /* connected, but the send failed         */
    IPC_ERR_SUBSCRIBE   = -3,   /* could not register for deltas          */
    IPC_ERR_ADDRESS     = -4,   /* address string not valid for adapter   */
    IPC_ERR_STATE       = -5,   /* called out of order (e.g. run first)   */
} ipc_status_t;

/* Reserved key name, set aside for a future "publish the whole config once at
 * start-up so a late-joining dashboard can initialise" feature. Nothing emits
 * it today; it is declared here so no adapter or caller claims the name for
 * something else. Transport-neutral — it is only a key string. */
#define IPC_SNAPSHOT_KEY "__snapshot__"

/* -----------------------------------------------------------------
 * One configuration delta
 * ----------------------------------------------------------------*/
typedef struct {
    const char *key;
    const char *value;
} ipc_kv_t;

/* Invoked by ipc_run() for every delta received. The ipc_kv_t and the strings
 * it points at are valid only for the duration of the call. */
typedef void (*ipc_event_cb)(const ipc_kv_t *kv, void *user);

/* -----------------------------------------------------------------
 * The port
 * ----------------------------------------------------------------*/
typedef struct ipc_channel ipc_channel_t;

typedef struct {
    /* Identity, used in logs and --help output. */
    const char *name;             /* "dbus", "ubus", "unix-socket", ...   */
    const char *address_syntax;   /* one line describing valid addresses  */
    const char *default_address;  /* used when address is NULL            */

    ipc_status_t (*open)     (ipc_channel_t *ch, const char *address);
    void         (*close)    (ipc_channel_t *ch);
    ipc_status_t (*publish)  (ipc_channel_t *ch, const ipc_kv_t *kv);
    ipc_status_t (*subscribe)(ipc_channel_t *ch, ipc_event_cb cb, void *user);
    ipc_status_t (*run)      (ipc_channel_t *ch);
} ipc_transport_t;

struct ipc_channel {
    const ipc_transport_t *transport;
    void                  *state;   /* adapter-private */
};

/* -----------------------------------------------------------------
 * Selection and convenience wrappers
 * ----------------------------------------------------------------*/

/* The transport compiled into this binary. Never NULL. */
const ipc_transport_t *ipc_transport(void);

/* Wrappers over the vtable. They bind the channel to ipc_transport() on open
 * and guard against NULL function pointers, so callers stay free of vtable
 * plumbing. */
ipc_status_t ipc_open     (ipc_channel_t *ch, const char *address);
void         ipc_close    (ipc_channel_t *ch);
ipc_status_t ipc_publish  (ipc_channel_t *ch, const char *key, const char *value);
ipc_status_t ipc_subscribe(ipc_channel_t *ch, ipc_event_cb cb, void *user);
ipc_status_t ipc_run      (ipc_channel_t *ch);

/* Human-readable form of a status code. */
const char *ipc_strerror(ipc_status_t st);

#endif /* IPC_BACKEND_H */
