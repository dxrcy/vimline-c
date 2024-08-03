#include <ctype.h>
#include <locale.h>
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

void draw_box_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    move(y - 1, x - 1);
    addch(ACS_ULCORNER);
    for (uint32_t i = 0; i < w; ++i) {
        addch(ACS_HLINE);
    }
    addch(ACS_URCORNER);

    for (uint32_t i = 0; i < h; ++i) {
        move(y + i, x - 1);
        addch(ACS_VLINE);
        move(y + i, x + w);
        addch(ACS_VLINE);
    }

    move(y + h, x - 1);
    addch(ACS_LLCORNER);
    for (uint32_t i = 0; i < w; ++i) {
        addch(ACS_HLINE);
    }
    addch(ACS_LRCORNER);
}

int main() {
    initscr();
    noecho();
    cbreak();
    setlocale(LC_ALL, "");
    keypad(stdscr, TRUE);
    set_escdelay(0);

    enum VimMode mode = NORMAL;

    char input[MAX_INPUT] = {};
    uint32_t input_len = 0;
    uint32_t cursor = 0;

    char ch;

    while (TRUE) {
        draw_box_outline(ROW, COL, MAX_INPUT, 1);
        move(ROW, COL);
        for (uint32_t i = 0; i < MAX_INPUT; ++i) {
            printw("%c", i < input_len ? input[i] : ' ');
        }

        move(ROW + 3, 0);
        printw("mode:   %s\n", mode_name(mode));
        printw("len:    %d\n", input_len);
        printw("cursor: %d\n", cursor);
        printw("input:  %02x\n", ch);

        draw_cursor(mode, cursor);

        refresh();

        ch = getch();

        switch (mode) {
            case NORMAL:
                switch (ch) {
                    case 'q':
                        goto quit;
                    case 'h':
                    case KEY_LEFT:
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (cursor < MAX_INPUT - 1 && cursor < input_len - 1) {
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
                    case '^':
                    case '_':
                    case '0':
                        // TODO: Move to start of input, not 0
                        cursor = 0;
                        break;
                    case '$':
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
                    case 0x04:
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 0x05:
                        if (cursor < MAX_INPUT && cursor < input_len) {
                            ++cursor;
                        }
                        break;
                    case 0x7f:
                        if (cursor > 0 && input_len > 0) {
                            for (uint32_t i = cursor; i < input_len; ++i) {
                                input[i - 1] = input[i];
                            }
                            --input_len;
                            --cursor;
                        }
                        break;
                    default:
                        if (isprint(ch)) {
                            if (input_len < MAX_INPUT) {
                                for (uint32_t i = input_len; i >= cursor + 1;
                                     --i) {
                                    input[i] = input[i - 1];
                                }
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
