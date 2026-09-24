CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -pipe
PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin

TARGET = gentoo-installer
SRC = gentoo-installer.c

.PHONY: all clean install uninstall run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(SBINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(SBINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(SBINDIR)/$(TARGET)

run: $(TARGET)
	@echo "Starting the installer (requires root privileges)..."
	sudo ./$(TARGET)
