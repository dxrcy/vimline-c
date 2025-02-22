CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -lncurses

TARGET = vimline
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

install:
	install -d $(BINDIR)
	install $(TARGET) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: install uninstall clean run

