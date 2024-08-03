CFLAGS=-Wall -Wextra -Wpedantic -lncurses

run:
	make main && ./main

main: main.c
	$(CC) $(CFLAGS) -o main main.c

