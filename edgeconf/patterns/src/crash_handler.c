/* src/crash_handler.c
 *
 * Async-signal-safe crash handler.
 *
 * Rules followed inside the signal handler:
 *   - Only write() for text output (async-signal-safe per POSIX).
 *   - backtrace_symbols_fd() is used instead of backtrace_symbols() to avoid
 *     malloc; this is a glibc extension accepted in practice for crash handlers.
 *   - No fprintf, no strsignal, no sprintf – these are NOT async-signal-safe.
 *   - Pre-built static strings are used for all fixed text.
 *   - SA_RESETHAND lets the kernel restore SIG_DFL before delivering the
 *     signal again after raise(), enabling core dumps without a recursive
 *     handler call.
 */
#include "crash_handler.h"

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <execinfo.h>

/* Pre-formatted strings – computed once at compile time, never at signal time */
static const char MSG_HEADER[]  = "\n*** CRASH DETECTED ***\nSignal: ";
static const char MSG_SIGSEGV[] = "SIGSEGV (Segmentation Fault)\n";
static const char MSG_SIGABRT[] = "SIGABRT (Abort)\n";
static const char MSG_SIGFPE[]  = "SIGFPE (Floating Point Exception)\n";
static const char MSG_SIGBUS[]  = "SIGBUS (Bus Error)\n";
static const char MSG_UNKNOWN[] = "Unknown\n";
static const char MSG_TRACE[]   = "Stack trace (most recent first):\n";

static void crash_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)info;
    (void)ucontext;

    void *frames[32];
    int   nframes;

    /* Fixed header */
    write(STDERR_FILENO, MSG_HEADER, sizeof(MSG_HEADER) - 1);

    /* Signal name without strsignal() */
    const char *signame;
    switch (sig) {
    case SIGSEGV: signame = MSG_SIGSEGV; break;
    case SIGABRT: signame = MSG_SIGABRT; break;
    case SIGFPE:  signame = MSG_SIGFPE;  break;
    case SIGBUS:  signame = MSG_SIGBUS;  break;
    default:      signame = MSG_UNKNOWN; break;
    }
    write(STDERR_FILENO, signame, strlen(signame));

    /* Stack trace */
    write(STDERR_FILENO, MSG_TRACE, sizeof(MSG_TRACE) - 1);
    nframes = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
    backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

    /*
     * SA_RESETHAND already restores SIG_DFL before we get here, so raise()
     * delivers the signal with default disposition (core dump / termination).
     */
    raise(sig);
}

void crash_handler_install(void) {
    struct sigaction sa;
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    /* SA_RESETHAND: restore SIG_DFL after first delivery (prevents recursion).
     * SA_SIGINFO:   give the handler the extended siginfo_t argument.       */
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
}
