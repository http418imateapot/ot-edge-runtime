/* src/watchdog.h
 *
 * Minimal sd_notify implementation for systemd watchdog integration.
 *
 * This implementation communicates directly over the NOTIFY_SOCKET Unix
 * datagram socket, avoiding a runtime dependency on libsystemd.
 *
 * Usage in the main loop:
 *   watchdog_ready();          // after startup is complete
 *   watchdog_ping();           // periodically, < WATCHDOG_USEC/2
 *
 * When not running under systemd (NOTIFY_SOCKET unset), all calls are no-ops.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* Send an arbitrary sd_notify message.
 * Returns 0 if sent or if NOTIFY_SOCKET is not set; -1 on socket error. */
int watchdog_notify(const char *msg);

/* Convenience wrappers */
static inline int watchdog_ready(void) { return watchdog_notify("READY=1"); }
static inline int watchdog_ping(void)  { return watchdog_notify("WATCHDOG=1"); }

/* Returns half of WATCHDOG_USEC (the recommended ping interval), or 0 if
 * the environment variable is not set (i.e. not under systemd watchdog). */
uint64_t watchdog_usec(void);

#endif /* WATCHDOG_H */
