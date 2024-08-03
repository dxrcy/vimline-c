#include <ctype.h>
#include <ncurses.h>

#define MAX_INPUT 20

enum VimMode {
    NORMAL,
    INSERT,
};

const uint32_t COL = 2;
const uint32_t ROW = 2;

const char* mode_name(enum VimMode mode) {
    switch (mode) {
        case NORMAL:
            return "NORMAL";
        case INSERT:
            return "INSERT";
        default:
            return "?";
    }
}

void draw_cursor(enum VimMode mode, uint32_t cursor) {
    int32_t offset = 0;
    switch (mode) {
        case INSERT:
            printf("\033[5 q");
            break;
        default:
            printf("\033[1 q");
            break;
    }
    fflush(stdout);
    move(ROW, COL + cursor + offset);
}

int main() {
    initscr();
    noecho();
    cbreak();
    /* keypad(stdscr, TRUE); */

    enum VimMode mode = NORMAL;

    char input[MAX_INPUT] = {};
    uint32_t input_len = 0;
    uint32_t cursor = 0;

    while (TRUE) {
        move(ROW - 1, COL - 1);
        for (uint32_t i = 0; i < MAX_INPUT + 2; ++i) {
            printw("-");
        }
        move(ROW, COL - 1);
        printw("|");
        for (uint32_t i = 0; i < MAX_INPUT; ++i) {
            printw("%c", i < input_len ? input[i] : ' ');
        }
        printw("|");
        move(ROW + 1, COL - 1);
        for (uint32_t i = 0; i < MAX_INPUT + 2; ++i) {
            printw("-");
        }

        printw("\n");
        printw("mode:   %s\n", mode_name(mode));
        printw("len:    %d\n", input_len);
        printw("cursor: %d\n", cursor);

        draw_cursor(mode, cursor);

        refresh();

        char ch = getch();

        /* printw("%0x\n", ch); */
        /* refresh(); */

        switch (mode) {
            case NORMAL:
                switch (ch) {
                    case 'q':
                        goto quit;
                    case 'h':
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 'l':
                        if (cursor < MAX_INPUT && cursor < input_len) {
                            ++cursor;
                        }
                        break;
                    case 'i':
                        mode = INSERT;
                        break;
                    case 'a':
                        mode = INSERT;
                        if (cursor < input_len) {
                            ++cursor;
                        }
                        break;
                    case 'I':
                        mode = INSERT;
                        cursor = 0;
                        break;
                    case 'A':
                        mode = INSERT;
                        cursor = input_len;
                        break;
                    case 'D':
                        input_len = cursor;
                        break;
                    default:
                        break;
                }
                break;

            case INSERT:
                switch (ch) {
                    case 0x1b:
                        mode = NORMAL;
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 0x7f:
                        if (input_len > 0) {
                            --input_len;
                            --cursor;
                        }
                        break;
                    default:
                        if (isprint(ch)) {
                            if (input_len < MAX_INPUT) {
                                for (uint32_t i = cursor + 1; i < input_len;
                                     ++i) {
                                    input[i] = input[i - 1];
                                }
                                // 012345
                                // abcxde
                                input[cursor] = ch;
                                ++cursor;
                                ++input_len;
                            }
                        }
                        break;
                };
                break;
        }
    }

quit:
    endwin();
    return 0;
}
