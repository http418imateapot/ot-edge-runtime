# Makefile for robust_config
#
# Targets:
#   all          Build the binary (default)
#   clean        Remove generated files
#   install      Install binary, D-Bus policy and systemd unit
#   uninstall    Remove installed files
#   test         Run unit tests (no D-Bus required)
#
# Cross-compile example:
#   make CC=aarch64-linux-gnu-gcc

CC      ?= gcc
CFLAGS   = -Wall -Wextra -g $(shell pkg-config --cflags dbus-1)
LDFLAGS  = $(shell pkg-config --libs dbus-1)

SRCDIR   = src
OBJDIR   = obj
TARGET   = robust_config

SRCS     = $(SRCDIR)/robust_config.c  \
           $(SRCDIR)/logger.c         \
           $(SRCDIR)/crash_handler.c  \
           $(SRCDIR)/config_io.c      \
           $(SRCDIR)/dbus_ipc.c       \
           $(SRCDIR)/watchdog.c

OBJS     = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

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
	@echo "=== Running unit tests ==="
	@bash tests/test_write_read.sh ./$(TARGET)
	@echo "=== All unit tests passed ==="

