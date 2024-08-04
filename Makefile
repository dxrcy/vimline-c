CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -lncurses
TARGET=vimput
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

dev:
	$(MAKE) $(TARGET) && ./$(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

install: $(TARGET)
	install -d $(BINDIR)
	install $(TARGET) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: dev install uninstall clean

