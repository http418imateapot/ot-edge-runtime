# Makefile for robust_config application
# This application supports two modes:
# 1. write - Safely writes logs to logs/log.txt
# 2. watch - Monitors changes in logs/log.txt and sends DBus signals

CC = gcc
CFLAGS = -Wall -g $(shell pkg-config --cflags dbus-1)
LDFLAGS = $(shell pkg-config --libs dbus-1)

SRCDIR = src
OBJDIR = obj
TARGET = robust_config

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJDIR)/robust_config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

