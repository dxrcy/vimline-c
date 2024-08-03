#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <string.h>

#define MAX_INPUT 20

enum VimMode {
    NORMAL,
    INSERT,
    REPLACE,
};

const uint32_t COL = 2;
const uint32_t ROW = 2;

const char* mode_name(enum VimMode mode) {
    switch (mode) {
        case NORMAL:
            return "NORMAL";
        case INSERT:
            return "INSERT";
        case REPLACE:
            return "REPLACE";
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
    // Top
    move(y - 1, x - 1);
    addch(ACS_ULCORNER);
    for (uint32_t i = 0; i < w; ++i) {
        addch(ACS_HLINE);
    }
    addch(ACS_URCORNER);
    // Sides
    for (uint32_t i = 0; i < h; ++i) {
        move(y + i, x - 1);
        addch(ACS_VLINE);
        move(y + i, x + w);
        addch(ACS_VLINE);
    }
    // Bottom
    move(y + h, x - 1);
    addch(ACS_LLCORNER);
    for (uint32_t i = 0; i < w; ++i) {
        addch(ACS_HLINE);
    }
    addch(ACS_LRCORNER);
}

int find_word_start(char* input, uint32_t cursor, uint32_t input_len) {
    // Empty line
    if (input_len < 1) {
        return 0;
    }
    // At end of line
    if (cursor + 1 >= input_len) {
        return input_len - 1;
    }
    // On a space
    // Look for first non-space character
    if (isspace(input[cursor])) {
        while (cursor + 1 < input_len) {
            ++cursor;
            if (!isspace(input[cursor])) {
                return cursor;
            }
        }
    }
    // On non-space
    int alnum = isalnum(input[cursor]);
    while (cursor < input_len) {
        ++cursor;
        // Space found
        // Look for first non-space character
        if (isspace(input[cursor])) {
            while (cursor + 1 < input_len) {
                ++cursor;
                if (!isspace(input[cursor])) {
                    return cursor;
                }
            }
            break;
        }
        // First punctuation after word
        // OR first word after punctuation
        if (isalnum(input[cursor]) != alnum) {
            return cursor;
        }
    }
    // No next word found
    // Go to end of line
    return input_len - 1;
}

int find_word_end(char* input, uint32_t cursor, uint32_t input_len) {
    // Empty line
    if (input_len < 1) {
        return 0;
    }
    // At end of line
    if (cursor + 1 >= input_len) {
        return input_len - 1;
    }
    // On a sequence of spaces (>=1)
    // Look for start of next word, start from there instead
    while (cursor + 1 < input_len && isspace(input[cursor])) {
        ++cursor;
    }
    // On non-space
    int alnum = isalnum(input[cursor]);
    while (cursor < input_len) {
        ++cursor;
        // Space found
        // Word ends at previous index
        // OR first punctuation after word
        // OR first word after punctuation
        if (isspace(input[cursor]) || isalnum(input[cursor]) != alnum) {
            return cursor - 1;
        }
    }
    // No next word found
    // Go to end of line
    return input_len - 1;
}

int find_word_back(char* input, uint32_t cursor) {
    // At start of line
    if (cursor <= 1) {
        return 0;
    }
    // Start at previous character
    --cursor;
    // On a sequence of spaces (>=1)
    // Look for end of previous word, start from there instead
    while (cursor > 0 && isspace(input[cursor])) {
        --cursor;
    }
    // Now on a non-space
    int alnum = isalnum(input[cursor]);
    while (cursor > 0) {
        cursor--;
        // Space found
        // OR first punctuation before word
        // OR first word before punctuation
        // Word starts at next index
        if (isspace(input[cursor]) || isalnum(input[cursor]) != alnum) {
            return cursor + 1;
        }
    }
    // No previous word found
    // Go to start of line
    return 0;
}

int main() {
    initscr();
    noecho();
    cbreak();
    setlocale(LC_ALL, "");
    keypad(stdscr, TRUE);
    set_escdelay(0);

    enum VimMode mode = NORMAL;

    char input[MAX_INPUT] = "abc  =def==( )";
    uint32_t input_len = strlen(input);
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
                    case 'r':
                        mode = REPLACE;
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
                    case 'w':
                        cursor = find_word_start(input, cursor, input_len);
                        break;
                    case 'e':
                        cursor = find_word_end(input, cursor, input_len);
                        break;
                    case 'b':
                        cursor = find_word_back(input, cursor);
                        break;
                    case '^':
                    case '_':
                    case '0':
                        // TODO: Move to start of input, not 0
                        cursor = 0;
                        break;
                    case '$':
                        cursor = input_len - 1;
                        break;
                    case 'D':
                        input_len = cursor;
                        break;
                    case 'x':
                        if (input_len > 0) {
                            for (uint32_t i = cursor + 1; i < input_len; ++i) {
                                input[i - 1] = input[i];
                            }
                            --input_len;
                            if (cursor >= input_len && input_len > 0) {
                                cursor = input_len - 1;
                            }
                        }
                        break;
                    default:
                        break;
                }
                break;

            case INSERT:
                switch (ch) {
                    case 0x1b:  // Escape
                        mode = NORMAL;
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 0x04:  // Left arrow
                        if (cursor > 0) {
                            --cursor;
                        }
                        break;
                    case 0x05:  // Right arrow
                        if (cursor < MAX_INPUT && cursor < input_len) {
                            ++cursor;
                        }
                        break;
                    case 0x07:  // Backspace
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

            case REPLACE:
                input[cursor] = ch;
                mode = NORMAL;
                break;
        }
    }

quit:
    endwin();
    return 0;
}
