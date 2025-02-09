/* src/robust_config.c */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <execinfo.h>   // For obtaining backtrace
#include <fcntl.h>      // For open() and O_RDONLY
#include <sys/file.h>   // For flock()
#include <sys/inotify.h> // For inotify API

#define TIME_BUF_SIZE 26
#define LOG_DIR "./logs"
#define LOG_FILE "logs/log.txt"
#define BUFFER_SIZE 1024

#define OBJECT_PATH "/com/example/LogWatcher"
#define INTERFACE_NAME "com.example.LogWatcher"
#define SIGNAL_NAME "NewLog"

/* ----------------------------------------------------
 * Crash handler: Catches fatal signals using sigaction,
 * prints the error and stack trace, then restores the
 * default signal handler and re-raises the signal to
 * allow the system to generate a core dump if enabled.
 * ----------------------------------------------------
 */
void crash_handler(int sig, siginfo_t *info, void *ucontext) {
    void *array[10];
    size_t size;

    fprintf(stderr, "\nCaught signal %d (%s)\n", sig, strsignal(sig));

    // Obtain stack trace (up to 10 frames)
    size = backtrace(array, 10);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    // Restore default signal handler and re-raise the signal
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ----------------------------------------------------
 * Print program usage information.
 * ----------------------------------------------------
 */
void print_usage(const char *prog_name) {
    printf("Usage: %s <mode>\n", prog_name);
    printf("  mode: write      - Write a log entry\n");
    printf("        watch      - Watch log file and send DBus signals on changes\n");
    printf("        dashboard  - Receive DBus signals and print log messages\n");
}

/* ----------------------------------------------------
 * Function to write a log entry (write mode).
 * ----------------------------------------------------
 */
int write_log() {
    struct stat st = {0};
    /* Check and create log directory */
    if (stat(LOG_DIR, &st) == -1) {
        if (mkdir(LOG_DIR, 0755) == -1) {
            perror("Failed to create log directory");
            return EXIT_FAILURE;
        }
    }
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp == NULL) {
        perror("Failed to open log file for writing");
        return EXIT_FAILURE;
    }
    /* Get current time and safely convert it */
    time_t now = time(NULL);
    char time_buf[TIME_BUF_SIZE];
    if (ctime_r(&now, time_buf) == NULL) {
        perror("Failed to convert time");
        fclose(fp);
        return EXIT_FAILURE;
    }
    time_buf[TIME_BUF_SIZE - 1] = '\0';  // Ensure string termination

    /* Write log entry */
    if (fprintf(fp, "Log entry at %s", time_buf) < 0) {
        perror("Failed to write log entry");
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);
    printf("Log entry written successfully to %s\n", LOG_FILE);
    return EXIT_SUCCESS;
}

/* ----------------------------------------------------
 * Function to safely read the log file.
 * Opens the file in read-only mode and uses a shared lock
 * to avoid interfering with concurrent writes.
 * ----------------------------------------------------
 */
int safe_read_log(char *content, size_t content_size) {
    int fd = open(LOG_FILE, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open log file for reading");
        return -1;
    }

    // Acquire a shared lock (non-blocking)
    if (flock(fd, LOCK_SH | LOCK_NB) == -1) {
        perror("Failed to acquire shared lock");
        close(fd);
        return -1;
    }

    ssize_t bytes_read = read(fd, content, content_size - 1);
    if (bytes_read < 0) {
        perror("Error reading log file");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }
    content[bytes_read] = '\0'; // Ensure string termination

    // Release the lock and close the file
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

/* ----------------------------------------------------
 * Function to send a D-Bus signal.
 * ----------------------------------------------------
 */
void send_dbus_signal(const char *log_message) {
    DBusError err;
    dbus_error_init(&err);

    /* Connect to the session bus (use DBUS_BUS_SYSTEM if needed) */
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus Connection Error: %s\n", err.message);
        dbus_error_free(&err);
        return;
    }
    if (conn == NULL) {
        fprintf(stderr, "DBus connection is NULL\n");
        return;
    }

    /* Create a signal message */
    DBusMessage *msg = dbus_message_new_signal(OBJECT_PATH, INTERFACE_NAME, SIGNAL_NAME);
    if (msg == NULL) {
        fprintf(stderr, "Failed to create DBus message\n");
        return;
    }
    /* Append log_message as an argument */
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &log_message, DBUS_TYPE_INVALID)) {
        fprintf(stderr, "Failed to append arguments to DBus message\n");
        dbus_message_unref(msg);
        return;
    }
    /* Send the message */
    if (!dbus_connection_send(conn, msg, NULL)) {
        fprintf(stderr, "Failed to send DBus message: Out Of Memory\n");
    }
    dbus_connection_flush(conn);
    dbus_message_unref(msg);
    printf("Sent DBus signal with log: %s\n", log_message);
}

/* ----------------------------------------------------
 * Function to monitor log file changes (watch mode).
 * Uses inotify to receive file change events.
 * ----------------------------------------------------
 */
int watch_log() {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("Failed to initialize inotify");
        return EXIT_FAILURE;
    }

    int wd = inotify_add_watch(inotify_fd, LOG_FILE, IN_MODIFY);
    if (wd < 0) {
        perror("Failed to add inotify watch");
        close(inotify_fd);
        return EXIT_FAILURE;
    }

    printf("Monitoring %s for changes...\n", LOG_FILE);

    char buffer[BUFFER_SIZE];
    while (1) {
        int length = read(inotify_fd, buffer, sizeof(buffer));
        if (length < 0) {
            perror("Error reading inotify events");
            break;
        }

        struct inotify_event *event = (struct inotify_event *)buffer;
        if (event->mask & IN_MODIFY) {
            char content[BUFFER_SIZE] = {0};
            if (safe_read_log(content, sizeof(content)) == 0) {
                send_dbus_signal(content);
            } else {
                fprintf(stderr, "Failed to safely read log file\n");
            }
        }
    }

    inotify_rm_watch(inotify_fd, wd);
    close(inotify_fd);
    return EXIT_SUCCESS;
}

/* ----------------------------------------------------
 * Function to receive and print D-Bus messages (dashboard mode).
 * ----------------------------------------------------
 */
int dashboard() {
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus Connection Error: %s\n", err.message);
        dbus_error_free(&err);
        return EXIT_FAILURE;
    }
    if (conn == NULL) {
        fprintf(stderr, "DBus connection is NULL\n");
        return EXIT_FAILURE;
    }

    char match_rule[256];
    snprintf(match_rule, sizeof(match_rule),
             "type='signal',interface='%s',member='%s'",
             INTERFACE_NAME, SIGNAL_NAME);
    dbus_bus_add_match(conn, match_rule, &err);
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus Add Match Error: %s\n", err.message);
        dbus_error_free(&err);
        return EXIT_FAILURE;
    }

    printf("Listening for D-Bus signals...\n");

    while (1) {
        dbus_connection_read_write(conn, 0);
        DBusMessage *msg = dbus_connection_pop_message(conn);
        if (msg == NULL) {
            usleep(100000);  // Sleep for 100ms
            continue;
        }

        if (dbus_message_is_signal(msg, INTERFACE_NAME, SIGNAL_NAME)) {
            const char *received_msg;
            if (dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &received_msg, DBUS_TYPE_INVALID)) {
                printf("Received message: %s\n", received_msg);
            } else {
                fprintf(stderr, "Failed to get message arguments: %s\n", err.message);
                dbus_error_free(&err);
            }
        }
        dbus_message_unref(msg);
    }
    return EXIT_SUCCESS;
}

/* ----------------------------------------------------
 * Main function: Chooses the mode of operation based
 * on command-line arguments and sets up signal handlers.
 * ----------------------------------------------------
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Register crash_handler to handle SIGSEGV, SIGABRT, and SIGFPE */
    struct sigaction sa;
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("Error setting SIGSEGV handler");
    }
    if (sigaction(SIGABRT, &sa, NULL) == -1) {
        perror("Error setting SIGABRT handler");
    }
    if (sigaction(SIGFPE, &sa, NULL) == -1) {
        perror("Error setting SIGFPE handler");
    }

    /* Choose mode based on command-line argument */
    if (strcmp(argv[1], "write") == 0) {
        return write_log();
    } else if (strcmp(argv[1], "watch") == 0) {
        return watch_log();
    } else if (strcmp(argv[1], "dashboard") == 0) {
        return dashboard();
    } else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}

