/* include/ipc_backend.h
 *
 * Compile-time selectable IPC back-end abstraction.
 *
 * Select the back-end at build time via the IPC_BACKEND make variable:
 *   make IPC_BACKEND=dbus   (default)
 *   make IPC_BACKEND=ubus
 *
 * All callers include only this header; the concrete implementation is
 * provided by ipc_dbus.c or ipc_ubus.c depending on which object is
 * linked into the final binary.
 */

#ifndef IPC_BACKEND_H
#define IPC_BACKEND_H

#include <stddef.h>

/* Callback type invoked by ipc_listen() for every received message. */
typedef void (*ipc_message_handler_t)(const char *message);

/* ----------------------------------------------------
 * ipc_init
 *   Establish a connection to the underlying IPC bus.
 *   Must be called before ipc_send_signal() or ipc_listen().
 *   Returns 0 on success, -1 on error.
 * ----------------------------------------------------
 */
int ipc_init(void);

/* ----------------------------------------------------
 * ipc_cleanup
 *   Release all resources acquired by ipc_init().
 *   Safe to call even if ipc_init() failed.
 * ----------------------------------------------------
 */
void ipc_cleanup(void);

/* ----------------------------------------------------
 * ipc_send_signal
 *   Broadcast message over the IPC bus.
 *   Returns 0 on success, -1 on error.
 * ----------------------------------------------------
 */
int ipc_send_signal(const char *message);

/* ----------------------------------------------------
 * ipc_listen
 *   Block indefinitely, invoking handler for each
 *   received message.  Returns -1 if setup fails.
 * ----------------------------------------------------
 */
int ipc_listen(ipc_message_handler_t handler);

#endif /* IPC_BACKEND_H */
