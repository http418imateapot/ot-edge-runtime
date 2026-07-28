/* src/ipc_ubus.c
 *
 * ubus back-end implementation of the ipc_backend.h interface.
 * Selected at build time with: make IPC_BACKEND=ubus
 *
 * Requires: libubus, libubox (OpenWrt or compatible distribution).
 *   Ubuntu/Debian: apt-get install libubus-dev libubox-dev
 *
 * Signals are mapped to ubus events on the topic defined by
 * UBUS_EVENT_TYPE.  The "message" blobmsg field carries the payload.
 */

#include <stdio.h>
#include <string.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>

#include "ipc_backend.h"

#define UBUS_EVENT_TYPE "com.example.logwatcher"
#define UBUS_MSG_FIELD  "message"

static struct ubus_context *g_ctx = NULL;
static ipc_message_handler_t g_handler = NULL;

/* blobmsg policy for the single "message" string field */
static const struct blobmsg_policy g_event_policy[] = {
    { .name = UBUS_MSG_FIELD, .type = BLOBMSG_TYPE_STRING },
};
#define N_EVENT_FIELDS (sizeof(g_event_policy) / sizeof(g_event_policy[0]))

/* ----------------------------------------------------
 * on_ubus_event – called by uloop for each matching event
 * ----------------------------------------------------
 */
static void on_ubus_event(struct ubus_context *ctx,
                          struct ubus_event_handler *ev,
                          const char *type,
                          struct blob_attr *msg) {
    (void)ctx;
    (void)ev;
    (void)type;

    if (g_handler == NULL)
        return;

    struct blob_attr *tb[N_EVENT_FIELDS];
    blobmsg_parse(g_event_policy, N_EVENT_FIELDS, tb,
                  blob_data(msg), blob_len(msg));

    if (tb[0] != NULL)
        g_handler(blobmsg_get_string(tb[0]));
}

static struct ubus_event_handler g_event_handler = { .cb = on_ubus_event };

/* ----------------------------------------------------
 * ipc_init
 * ----------------------------------------------------
 */
int ipc_init(void) {
    g_ctx = ubus_connect(NULL);
    if (g_ctx == NULL) {
        fprintf(stderr, "Failed to connect to ubus\n");
        return -1;
    }
    return 0;
}

/* ----------------------------------------------------
 * ipc_cleanup
 * ----------------------------------------------------
 */
void ipc_cleanup(void) {
    if (g_ctx != NULL) {
        ubus_free(g_ctx);
        g_ctx = NULL;
    }
}

/* ----------------------------------------------------
 * ipc_send_signal
 * ----------------------------------------------------
 */
int ipc_send_signal(const char *message) {
    if (g_ctx == NULL) {
        fprintf(stderr, "ubus not initialized\n");
        return -1;
    }

    /* Zero-init before blob_buf_init() to silence padding-byte warnings */
    struct blob_buf b = {};
    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, UBUS_MSG_FIELD, message);

    int ret = ubus_send_event(g_ctx, UBUS_EVENT_TYPE, b.head);
    blob_buf_free(&b);

    if (ret != UBUS_STATUS_OK) {
        fprintf(stderr, "Failed to send ubus event: %s\n", ubus_strerror(ret));
        return -1;
    }

    printf("Sent ubus event with log: %s\n", message);
    return 0;
}

/* ----------------------------------------------------
 * ipc_listen
 * ----------------------------------------------------
 */
int ipc_listen(ipc_message_handler_t handler) {
    if (g_ctx == NULL) {
        fprintf(stderr, "ubus not initialized\n");
        return -1;
    }

    g_handler = handler;

    int ret = ubus_register_event_handler(g_ctx, &g_event_handler, UBUS_EVENT_TYPE);
    if (ret != UBUS_STATUS_OK) {
        fprintf(stderr, "Failed to register ubus event handler: %s\n", ubus_strerror(ret));
        return -1;
    }

    uloop_init();
    ubus_add_uloop(g_ctx);

    printf("Listening for ubus events...\n");
    uloop_run();
    uloop_done();
    return 0;
}
