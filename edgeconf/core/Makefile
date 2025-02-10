CC := gcc
CFLAGS := -Wall -Wextra -O2
SRCDIR = src
OBJDIR = obj
TARGET = robust_bin_config

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJDIR)/robust_bin_config.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

