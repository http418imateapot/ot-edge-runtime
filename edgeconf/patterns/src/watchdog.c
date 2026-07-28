/* src/watchdog.c
 *
 * Minimal sd_notify without depending on libsystemd.
 * Protocol: write a newline-terminated status string to the Unix datagram
 * socket whose path is in $NOTIFY_SOCKET.  Abstract sockets are addressed
 * with a leading '@' (replaced by NUL in sun_path).
 */
#include "watchdog.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>

int watchdog_notify(const char *msg) {
    const char *sock_path = getenv("NOTIFY_SOCKET");
    if (!sock_path || !*sock_path)
        return 0;   /* Not running under systemd notify */

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        log_warn("watchdog_notify: socket() failed: %m");
        return -1;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;

    socklen_t sa_len;
    if (sock_path[0] == '@') {
        /* Abstract namespace socket */
        size_t path_len = strlen(sock_path + 1);
        if (path_len >= sizeof(sa.sun_path) - 1) {
            close(fd);
            return -1;
        }
        sa.sun_path[0] = '\0';
        memcpy(sa.sun_path + 1, sock_path + 1, path_len);
        sa_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                             + 1 + path_len);
    } else {
        size_t path_len = strlen(sock_path);
        if (path_len >= sizeof(sa.sun_path)) {
            close(fd);
            return -1;
        }
        memcpy(sa.sun_path, sock_path, path_len + 1);
        sa_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                             + path_len + 1);
    }

    ssize_t n = sendto(fd, msg, strlen(msg), MSG_NOSIGNAL,
                       (struct sockaddr *)&sa, sa_len);
    close(fd);

    if (n < 0) {
        log_warn("watchdog_notify: sendto() failed: %m");
        return -1;
    }
    return 0;
}

uint64_t watchdog_usec(void) {
    const char *s = getenv("WATCHDOG_USEC");
    if (!s || !*s)
        return 0;
    uint64_t usec = (uint64_t)strtoull(s, NULL, 10);
    return usec / 2;   /* Ping at half the configured watchdog interval */
}
