# Makefile for robust_config application
# This application supports three modes:
# 1. write     - Safely writes logs to logs/log.txt
# 2. watch     - Monitors changes in logs/log.txt and sends IPC signals
# 3. dashboard - Receives IPC signals and prints log messages
#
# Compile-time IPC back-end selection (default: dbus):
#   make IPC_BACKEND=dbus
#   make IPC_BACKEND=ubus

CC     = gcc
SRCDIR = src
OBJDIR = obj
TARGET = robust_config

# Supported values: dbus (default, requires libdbus-1-dev)
#                   ubus (requires libubus-dev libubox-dev, OpenWrt or compatible)
# Any other value falls through to the dbus path.
IPC_BACKEND ?= dbus

ifeq ($(IPC_BACKEND),ubus)
    IPC_CFLAGS  = $(shell pkg-config --cflags libubus libubox) -DIPC_BACKEND_UBUS
    IPC_LDFLAGS = $(shell pkg-config --libs   libubus libubox)
    IPC_OBJ     = $(OBJDIR)/ipc_ubus.o
else
    IPC_CFLAGS  = $(shell pkg-config --cflags dbus-1) -DIPC_BACKEND_DBUS
    IPC_LDFLAGS = $(shell pkg-config --libs   dbus-1)
    IPC_OBJ     = $(OBJDIR)/ipc_dbus.o
endif

CFLAGS  = -Wall -g -I./include $(IPC_CFLAGS)
LDFLAGS = $(IPC_LDFLAGS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJDIR)/robust_config.o $(IPC_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

