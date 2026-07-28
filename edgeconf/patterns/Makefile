# Makefile for robust_config
#
# Targets:
#   all          Build the binary (default)
#   clean        Remove generated files
#   install      Install binary, D-Bus policy and systemd unit
#   uninstall    Remove installed files
#   test         Run unit tests (no bus required)
#
# IPC transport selection
# -----------------------
# The daemon talks to the outside world through the port declared in
# include/ipc_backend.h. Exactly one adapter is linked in, chosen here:
#
#   make                     # IPC_BACKEND=dbus (default, the reference adapter)
#   make IPC_BACKEND=dbus    # same thing, spelled out
#
# Only the D-Bus adapter ships today. Adding another one — ubus for
# OpenWrt/uClinux targets, or a plain Unix domain socket for images with no bus
# at all — means writing src/ipc_<name>.c against that port and adding <name>
# to IPC_BACKENDS below. Nothing outside the adapter needs to change; see the
# contract at the top of include/ipc_backend.h.
#
# Cross-compile example:
#   make CC=aarch64-linux-gnu-gcc

CC      ?= gcc

SRCDIR   = src
OBJDIR   = obj
INCDIR   = include
TARGET   = robust_config

# ---------------------------------------------------------------------------
# Transport selection
# ---------------------------------------------------------------------------
IPC_BACKEND ?= dbus
IPC_BACKENDS = dbus

ifeq ($(filter $(IPC_BACKEND),$(IPC_BACKENDS)),)
$(error IPC_BACKEND='$(IPC_BACKEND)' is not implemented. \
Available: $(IPC_BACKENDS). See include/ipc_backend.h for the adapter contract)
endif

ifeq ($(IPC_BACKEND),dbus)
IPC_CFLAGS  = $(shell pkg-config --cflags dbus-1)
IPC_LDFLAGS = $(shell pkg-config --libs   dbus-1)
endif

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# Transport-independent core.
CORE_SRCS = $(SRCDIR)/robust_config.c \
            $(SRCDIR)/logger.c        \
            $(SRCDIR)/crash_handler.c \
            $(SRCDIR)/config_io.c     \
            $(SRCDIR)/watchdog.c      \
            $(SRCDIR)/ipc_backend.c

# The one adapter selected above.
IPC_SRC   = $(SRCDIR)/ipc_$(IPC_BACKEND).c

SRCS      = $(CORE_SRCS) $(IPC_SRC)
OBJS      = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

CFLAGS   += -Wall -Wextra -g -I$(INCDIR) -I$(SRCDIR) $(IPC_CFLAGS)
LDFLAGS  += $(IPC_LDFLAGS)

PREFIX  ?= /usr/local
DESTDIR ?=

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/robust_config
	install -d $(DESTDIR)/etc/dbus-1/system.d
	install -m 644 dbus/com.example.RobustConfig.conf \
	              $(DESTDIR)/etc/dbus-1/system.d/
	install -d $(DESTDIR)/lib/systemd/system
	install -m 644 systemd/robust-config-watch.service \
	              $(DESTDIR)/lib/systemd/system/
	install -d $(DESTDIR)/etc/robust-config

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/robust_config
	rm -f $(DESTDIR)/etc/dbus-1/system.d/com.example.RobustConfig.conf
	rm -f $(DESTDIR)/lib/systemd/system/robust-config-watch.service

test: $(TARGET)
	@echo "=== Running unit tests (transport: $(IPC_BACKEND)) ==="
	@bash tests/test_write_read.sh ./$(TARGET)
	@echo "=== All unit tests passed ==="
