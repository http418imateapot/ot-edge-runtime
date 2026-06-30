/* src/robust_config.c
 *
 * Edge Config Sync Daemon — industrial-grade configuration synchronisation
 * for embedded Linux systems (Jetson Nano, Raspberry Pi, i.MX8, …).
 *
 * Modes:
 *   write      --key KEY --value VAL   Atomically update one config key
 *   watch                              Monitor config file; broadcast D-Bus deltas
 *   dashboard                          Receive D-Bus signals and display them
 *   dump                               Print current config to stdout
 *
 * Config path resolution order:
 *   1. --config CLI option
 *   2. $ROBUST_CONFIG_PATH environment variable
 *   3. /etc/robust-config/config.conf  (compiled-in default)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>
#include <syslog.h>

#include "logger.h"
#include "crash_handler.h"
#include "config_io.h"
#include "dbus_ipc.h"
#include "watchdog.h"

/* -----------------------------------------------------------------
 * Defaults
 * ----------------------------------------------------------------*/
#define DEFAULT_CONFIG_FILE  "/etc/robust-config/config.conf"
#define ENV_CONFIG_PATH      "ROBUST_CONFIG_PATH"

/* -----------------------------------------------------------------
 * Program options
 * ----------------------------------------------------------------*/
typedef struct {
    const char *config_file;
    bus_type_t  bus_type;
    int         log_level;    /* syslog priority */
    int         log_to_stderr;
    int         dry_run;
    /* write mode */
    const char *write_key;
    const char *write_value;
} options_t;

static void print_usage(const char *prog) {
    printf(
        "Usage: %s [options] <mode>\n"
        "\n"
        "Modes:\n"
        "  write     Update one config key (requires --key and --value)\n"
        "  watch     Monitor config file and broadcast D-Bus delta signals\n"
        "  dashboard Receive D-Bus config signals and display them\n"
        "  dump      Print current config to stdout\n"
        "\n"
        "Options:\n"
        "  --config PATH          Config file path\n"
        "                         (default: $%s or %s)\n"
        "  --bus session|system   D-Bus bus type (default: system)\n"
        "  --log-level LEVEL      error|warn|info|debug (default: info)\n"
        "  --log-stderr           Log to stderr instead of syslog\n"
        "  --dry-run              Print actions without executing them\n"
        "  --key KEY              Key to write (write mode)\n"
        "  --value VAL            Value to write (write mode)\n"
        "  --help                 Show this help and exit\n",
        prog, ENV_CONFIG_PATH, DEFAULT_CONFIG_FILE
    );
}

static int parse_log_level(const char *s) {
    if (strcmp(s, "error") == 0) return LOG_ERR;
    if (strcmp(s, "warn")  == 0) return LOG_WARNING;
    if (strcmp(s, "info")  == 0) return LOG_INFO;
    if (strcmp(s, "debug") == 0) return LOG_DEBUG;
    /* Unknown level: warn the user and fall back to info */
    fprintf(stderr, "Warning: unknown log level '%s'; valid values are "
            "error|warn|info|debug. Defaulting to 'info'.\n", s);
    return LOG_INFO;
}

static int parse_options(int argc, char *argv[],
                         options_t *opts, int *mode_idx) {
    static const struct option long_opts[] = {
        { "config",     required_argument, NULL, 'c' },
        { "bus",        required_argument, NULL, 'b' },
        { "log-level",  required_argument, NULL, 'l' },
        { "log-stderr", no_argument,       NULL, 'e' },
        { "dry-run",    no_argument,       NULL, 'd' },
        { "key",        required_argument, NULL, 'k' },
        { "value",      required_argument, NULL, 'v' },
        { "help",       no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    /* Defaults */
    opts->config_file   = NULL;
    opts->bus_type      = BUS_SYSTEM;
    opts->log_level     = LOG_INFO;
    opts->log_to_stderr = 0;
    opts->dry_run       = 0;
    opts->write_key     = NULL;
    opts->write_value   = NULL;

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c': opts->config_file   = optarg; break;
        case 'b':
            if (strcmp(optarg, "session") == 0)      opts->bus_type = BUS_SESSION;
            else if (strcmp(optarg, "system") == 0)  opts->bus_type = BUS_SYSTEM;
            else {
                fprintf(stderr, "Unknown bus type '%s'\n", optarg);
                return -1;
            }
            break;
        case 'l': opts->log_level     = parse_log_level(optarg); break;
        case 'e': opts->log_to_stderr = 1;      break;
        case 'd': opts->dry_run       = 1;      break;
        case 'k': opts->write_key     = optarg; break;
        case 'v': opts->write_value   = optarg; break;
        case 'h': print_usage(argv[0]); exit(EXIT_SUCCESS);
        default:  return -1;
        }
    }

    *mode_idx = optind;
    return 0;
}

/* Config path: CLI > env > default */
static const char *resolve_config_path(const options_t *opts) {
    if (opts->config_file)
        return opts->config_file;
    const char *env = getenv(ENV_CONFIG_PATH);
    if (env && *env)
        return env;
    return DEFAULT_CONFIG_FILE;
}

/* -----------------------------------------------------------------
 * Mode: write
 * ----------------------------------------------------------------*/
static int mode_write(const options_t *opts, const char *config_path) {
    if (!opts->write_key || !opts->write_value) {
        fprintf(stderr, "write mode requires --key and --value\n");
        return EXIT_FAILURE;
    }

    if (opts->dry_run) {
        printf("[dry-run] Would write %s=%s to %s\n",
               opts->write_key, opts->write_value, config_path);
        return EXIT_SUCCESS;
    }

    if (config_update_key(config_path, opts->write_key, opts->write_value) != 0)
        return EXIT_FAILURE;

    log_info("Config updated: %s=%s -> %s",
             opts->write_key, opts->write_value, config_path);
    return EXIT_SUCCESS;
}

/* -----------------------------------------------------------------
 * Mode: dump
 * ----------------------------------------------------------------*/
static int mode_dump(const char *config_path) {
    config_t cfg;
    if (config_read(config_path, &cfg) != 0) {
        fprintf(stderr, "Cannot read config from '%s'\n", config_path);
        return EXIT_FAILURE;
    }
    printf("# Config: %s  (%d entries)\n", config_path, cfg.count);
    for (int i = 0; i < cfg.count; i++)
        printf("%s=%s\n", cfg.pairs[i].key, cfg.pairs[i].value);
    return EXIT_SUCCESS;
}

/* -----------------------------------------------------------------
 * Mode: watch
 *
 * Design:
 *   1. Watch the directory containing the config file for IN_MOVED_TO
 *      and IN_CLOSE_WRITE events (catches both atomic-rename and in-place
 *      writes).
 *   2. On any matching event, read the full config and diff against the
 *      previous snapshot.
 *   3. For each changed key, send one D-Bus signal carrying a JSON payload
 *      with interface_version, key and value.
 *   4. Periodically ping the systemd watchdog (if WATCHDOG_USEC is set).
 * ----------------------------------------------------------------*/
static int mode_watch(const options_t *opts, const char *config_path) {
    /* Resolve directory and filename components */
    char dir[512];
    strncpy(dir, config_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    const char *filename = config_path;
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash  = '\0';
        filename = slash + 1;
    } else {
        strncpy(dir, ".", sizeof(dir) - 1);
    }

    /* D-Bus connection */
    DBusConnection *conn = NULL;
    if (!opts->dry_run) {
        conn = dbus_ipc_connect(opts->bus_type);
        if (!conn)
            return EXIT_FAILURE;
    }

    /* inotify on the directory (catches atomic renames) */
    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) {
        log_err("inotify_init1 failed: %m");
        return EXIT_FAILURE;
    }
    int wd = inotify_add_watch(ifd, dir,
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) {
        log_err("inotify_add_watch on '%s' failed: %m", dir);
        close(ifd);
        return EXIT_FAILURE;
    }

    /* Initial config snapshot */
    config_t prev;
    memset(&prev, 0, sizeof(prev));
    config_read(config_path, &prev);

    /* Announce readiness to systemd */
    watchdog_ready();

    log_info("Watching '%s' for changes (bus: %s, dry-run: %s)",
             config_path,
             opts->bus_type == BUS_SYSTEM ? "system" : "session",
             opts->dry_run ? "yes" : "no");

    /* Watchdog interval in milliseconds (0 = no watchdog) */
    uint64_t wd_usec = watchdog_usec();
    int      poll_ms = (wd_usec > 0) ? (int)(wd_usec / 1000) : -1;

    char ev_buf[4096]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    while (1) {
        struct pollfd pfd = { .fd = ifd, .events = POLLIN };
        int ret = poll(&pfd, 1, poll_ms);

        if (ret < 0) {
            if (errno == EINTR)
                continue;
            log_err("poll failed: %m");
            break;
        }

        if (ret == 0) {
            /* Timeout: keepalive ping only */
            watchdog_ping();
            continue;
        }

        /* Read all pending inotify events */
        ssize_t len = read(ifd, ev_buf, sizeof(ev_buf));
        if (len < 0) {
            if (errno == EINTR)
                continue;
            log_err("read inotify events failed: %m");
            break;
        }

        int triggered = 0;
        for (char *ptr = ev_buf; ptr < ev_buf + len; ) {
            struct inotify_event *ev = (struct inotify_event *)ptr;
            if (ev->len > 0 &&
                strncmp(ev->name, filename, strlen(filename)) == 0)
                triggered = 1;
            ptr += sizeof(struct inotify_event) + ev->len;
        }

        if (!triggered) {
            watchdog_ping();
            continue;
        }

        /* Read updated config and compute diff */
        config_t cur;
        memset(&cur, 0, sizeof(cur));
        if (config_read(config_path, &cur) != 0) {
            log_warn("Failed to read config after change event; skipping");
            watchdog_ping();
            continue;
        }

        config_pair_t changed[CONFIG_MAX_PAIRS];
        int n = config_diff(&prev, &cur, changed, CONFIG_MAX_PAIRS);

        for (int i = 0; i < n; i++) {
            if (opts->dry_run) {
                printf("[dry-run] Signal: key=%s value=%s\n",
                       changed[i].key, changed[i].value);
            } else {
                dbus_ipc_send(conn, changed[i].key, changed[i].value);
            }
        }

        if (n > 0)
            log_info("%d key(s) changed in '%s'", n, config_path);

        prev = cur;
        watchdog_ping();
    }

    inotify_rm_watch(ifd, wd);
    close(ifd);
    return EXIT_FAILURE;   /* Should not reach here under normal operation */
}

/* -----------------------------------------------------------------
 * Mode: dashboard
 * ----------------------------------------------------------------*/
static int mode_dashboard(const options_t *opts) {
    if (opts->dry_run) {
        printf("[dry-run] Would listen on D-Bus %s bus for %s.%s\n",
               opts->bus_type == BUS_SYSTEM ? "system" : "session",
               DBUS_IFACE_NAME, DBUS_SIGNAL_NAME);
        return EXIT_SUCCESS;
    }

    DBusConnection *conn = dbus_ipc_connect(opts->bus_type);
    if (!conn)
        return EXIT_FAILURE;

    return dbus_ipc_dashboard(conn);
}

/* -----------------------------------------------------------------
 * main
 * ----------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    options_t opts;
    int mode_idx = 0;

    if (parse_options(argc, argv, &opts, &mode_idx) != 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (mode_idx >= argc) {
        fprintf(stderr, "No mode specified.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    logger_init("robust_config", opts.log_to_stderr, opts.log_level);
    crash_handler_install();

    const char *config_path = resolve_config_path(&opts);
    const char *mode        = argv[mode_idx];

    log_debug("Mode=%s config=%s bus=%s dry-run=%s",
              mode, config_path,
              opts.bus_type == BUS_SYSTEM ? "system" : "session",
              opts.dry_run ? "yes" : "no");

    int rc;
    if      (strcmp(mode, "write")     == 0) rc = mode_write(&opts, config_path);
    else if (strcmp(mode, "watch")     == 0) rc = mode_watch(&opts, config_path);
    else if (strcmp(mode, "dashboard") == 0) rc = mode_dashboard(&opts);
    else if (strcmp(mode, "dump")      == 0) rc = mode_dump(config_path);
    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        rc = EXIT_FAILURE;
    }

    logger_close();
    return rc;
}

