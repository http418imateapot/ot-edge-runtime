/* src/crash_handler.h
 * Installs async-signal-safe handlers for SIGSEGV, SIGABRT and SIGFPE.
 *
 * The handler uses only write() and backtrace_symbols_fd(), both of which
 * are safe to call from a signal handler (no stdio, no malloc, no strsignal).
 * After printing the stack trace it re-raises the signal with the default
 * disposition, allowing the kernel to produce a core dump if ulimits permit.
 */
#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

/* Register the crash handler for SIGSEGV, SIGABRT and SIGFPE. */
void crash_handler_install(void);

#endif /* CRASH_HANDLER_H */
