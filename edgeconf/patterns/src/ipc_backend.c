/* src/ipc_backend.c
 *
 * Transport-neutral plumbing: the wrappers declared in ipc_backend.h.
 *
 * The concrete adapter is chosen at build time (see IPC_BACKEND in the
 * Makefile) and reaches this file through ipc_transport(), which each adapter
 * defines. Nothing here knows what that adapter is.
 */

#include <stddef.h>

#include "ipc_backend.h"
#include "logger.h"

/* Bind the channel to the compiled-in transport and hand it the address.
 * `address` may be NULL, in which case the adapter's default is used. */
ipc_status_t ipc_open(ipc_channel_t *ch, const char *address) {
    if (ch == NULL)
        return IPC_ERR_STATE;

    const ipc_transport_t *t = ipc_transport();

    ch->transport = t;
    ch->state     = NULL;

    if (t->open == NULL) {
        log_err("ipc: transport '%s' has no open()", t->name);
        return IPC_ERR_STATE;
    }

    if (address == NULL)
        address = t->default_address;

    return t->open(ch, address);
}

void ipc_close(ipc_channel_t *ch) {
    if (ch == NULL || ch->transport == NULL)
        return;
    if (ch->transport->close != NULL)
        ch->transport->close(ch);
    ch->state = NULL;
}

ipc_status_t ipc_publish(ipc_channel_t *ch, const char *key, const char *value) {
    if (ch == NULL || ch->transport == NULL)
        return IPC_ERR_STATE;
    if (ch->transport->publish == NULL) {
        log_err("ipc: transport '%s' has no publish()", ch->transport->name);
        return IPC_ERR_STATE;
    }

    ipc_kv_t kv = { .key = key, .value = value, .deleted = 0 };
    return ch->transport->publish(ch, &kv);
}

ipc_status_t ipc_publish_removal(ipc_channel_t *ch, const char *key,
                                 const char *last_value) {
    if (ch == NULL || ch->transport == NULL)
        return IPC_ERR_STATE;
    if (ch->transport->publish == NULL) {
        log_err("ipc: transport '%s' has no publish()", ch->transport->name);
        return IPC_ERR_STATE;
    }

    ipc_kv_t kv = { .key = key, .value = last_value, .deleted = 1 };
    return ch->transport->publish(ch, &kv);
}

ipc_status_t ipc_subscribe(ipc_channel_t *ch, ipc_event_cb cb, void *user) {
    if (ch == NULL || ch->transport == NULL)
        return IPC_ERR_STATE;
    if (ch->transport->subscribe == NULL) {
        log_err("ipc: transport '%s' has no subscribe()", ch->transport->name);
        return IPC_ERR_STATE;
    }
    return ch->transport->subscribe(ch, cb, user);
}

ipc_status_t ipc_run(ipc_channel_t *ch) {
    if (ch == NULL || ch->transport == NULL)
        return IPC_ERR_STATE;
    if (ch->transport->run == NULL) {
        log_err("ipc: transport '%s' has no run()", ch->transport->name);
        return IPC_ERR_STATE;
    }
    return ch->transport->run(ch);
}

const char *ipc_strerror(ipc_status_t st) {
    switch (st) {
    case IPC_OK:              return "success";
    case IPC_ERR_CONNECT:     return "cannot reach IPC endpoint";
    case IPC_ERR_PUBLISH:     return "failed to publish delta";
    case IPC_ERR_SUBSCRIBE:   return "failed to subscribe to deltas";
    case IPC_ERR_ADDRESS:     return "invalid IPC address";
    case IPC_ERR_STATE:       return "IPC channel used out of order";
    }
    return "unknown IPC error";
}
