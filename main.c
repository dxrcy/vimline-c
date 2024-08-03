#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>

#define CTRL(key) key - 0x60
#define K_ESCAPE 0x1b
#define K_LEFT 0x104
#define K_RIGHT 0x105
#define K_BACKSPACE 0x107

#define MAX_INPUT 20
#define MAX_HISTORY 100

enum VimMode {
    NORMAL,
    INSERT,
    REPLACE,
};

typedef struct State {
    // Not null-terminated
    char input[MAX_INPUT];
    uint32_t input_len;
    uint32_t cursor;
} State;

typedef struct History {
    State states[MAX_HISTORY];
    uint32_t len;
    uint32_t index;
} History;

const uint32_t COL = 2;
const uint32_t ROW = 2;

enum VimMode mode = NORMAL;

State state = {
    .input = {0},
    .input_len = 0,
    .cursor = 0,
};

History history = {
    .states = {{{0}}},
    .len = 0,
    .index = 0,
};

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

int find_word_start(bool full_word) {
    // Empty line
    if (state.input_len < 1) {
        return 0;
    }
    // At end of line
    if (state.cursor + 1 >= state.input_len) {
        return state.input_len - 1;
    }
    // On a space
    // Look for first non-space character
    if (isspace(state.input[state.cursor])) {
        while (state.cursor + 1 < state.input_len) {
            ++state.cursor;
            if (!isspace(state.input[state.cursor])) {
                return state.cursor;
            }
        }
    }
    // On non-space
    int alnum = isalnum(state.input[state.cursor]);
    while (state.cursor < state.input_len) {
        ++state.cursor;
        // Space found
        // Look for first non-space character
        if (isspace(state.input[state.cursor])) {
            while (state.cursor + 1 < state.input_len) {
                ++state.cursor;
                if (!isspace(state.input[state.cursor])) {
                    return state.cursor;
                }
            }
            break;
        }
        // First punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (!full_word && isalnum(state.input[state.cursor]) != alnum) {
            return state.cursor;
        }
    }
    // No next word found
    // Go to end of line
    return state.input_len - 1;
}

int find_word_end(bool full_word) {
    // Empty line
    if (state.input_len < 1) {
        return 0;
    }
    // At end of line
    if (state.cursor + 1 >= state.input_len) {
        return state.input_len - 1;
    }
    // On a sequence of spaces (>=1)
    // Look for start of next word, start from there instead
    while (state.cursor + 1 < state.input_len &&
           isspace(state.input[state.cursor])) {
        ++state.cursor;
    }
    // On non-space
    int alnum = isalnum(state.input[state.cursor]);
    while (state.cursor < state.input_len) {
        ++state.cursor;
        // Space found
        // Word ends at previous index
        // OR first punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (isspace(state.input[state.cursor]) ||
            (!full_word && isalnum(state.input[state.cursor]) != alnum)) {
            return state.cursor - 1;
        }
    }
    // No next word found
    // Go to end of line
    return state.input_len - 1;
}

int find_word_back(bool full_word) {
    // At start of line
    if (state.cursor <= 1) {
        return 0;
    }
    // Start at previous character
    --state.cursor;
    // On a sequence of spaces (>=1)
    // Look for end of previous word, start from there instead
    while (state.cursor > 0 && isspace(state.input[state.cursor])) {
        --state.cursor;
    }
    // Now on a non-space
    int alnum = isalnum(state.input[state.cursor]);
    while (state.cursor > 0) {
        state.cursor--;
        // Space found
        // OR first punctuation before word
        // OR first word before punctuation
        // Word starts at next index
        // (If distinguishing words and punctuation)
        if (isspace(state.input[state.cursor]) ||
            (!full_word && isalnum(state.input[state.cursor]) != alnum)) {
            return state.cursor + 1;
        }
    }
    // No previous word found
    // Go to start of line
    return 0;
}

bool equals_state_input(const State* s1, const State* s2) {
    if (s1->input_len != s2->input_len) {
        return false;
    }
    for (uint32_t i = 0; i < s1->input_len; ++i) {
        if (s1->input[i] != s2->input[i]) {
            return false;
        }
    }
    return true;
}

void copy_state(State* src, State* dest) {
    dest->input_len = src->input_len;
    dest->cursor = src->cursor;
    if (dest->cursor > dest->input_len) {
        dest->cursor = dest->input_len == 0 ? 0 : dest->input_len - 1;
    }
    for (uint32_t i = 0; i < src->input_len; ++i) {
        dest->input[i] = src->input[i];
    }
}

void push_history() {
    // Delete all future history to be overwritten
    if (history.index <= history.len) {
        history.len = history.index;
    }
    // Ignore if same as last entry
    if (history.len > 0 &&
        equals_state_input(&state, &history.states[history.len - 1])) {
        return;
    }
    // TODO: Use cyclic array
    if (history.len >= MAX_HISTORY) {
        for (uint32_t i = 1; i < history.len; ++i) {
            copy_state(&history.states[i], &history.states[i - 1]);
        }
    } else {
        ++history.len;
        ++history.index;
    }

    copy_state(&state, &history.states[history.len - 1]);
}

void undo_history() {
    if (history.len == 0 || history.index == 0) {
        return;
    }
    --history.index;
    copy_state(&history.states[history.index], &state);
}
void redo_history() {
    if (history.index + 1 >= history.len) {
        return;
    }
    ++history.index;
    copy_state(&history.states[history.index], &state);
}

void terminate() {
    endwin();
    exit(0);
}

int main() {
    initscr();
    noecho();              // Disable echoing
    cbreak();              // Disable line buffering
    keypad(stdscr, TRUE);  // Enable raw key input
    set_escdelay(0);       // Disable Escape key delay

    signal(SIGINT, terminate);  // Clean up on SIGINT

    push_history();

    int key = 0;

    while (TRUE) {
        draw_box_outline(ROW, COL, MAX_INPUT, 1);
        move(ROW, COL);
        for (uint32_t i = 0; i < MAX_INPUT; ++i) {
            printw("%c", i < state.input_len ? state.input[i] : ' ');
        }

        move(ROW + 3, 0);
        printw("mode:    %s\n", mode_name(mode));
        printw("len:     %d\n", state.input_len);
        printw("cursor:  %d\n", state.cursor);
        printw("input:   0x%02x\n", key);
        printw("history: %d/%d\n", history.index, history.len);

        draw_cursor(mode, state.cursor);

        refresh();

        key = getch();

        switch (mode) {
            case NORMAL:
                switch (key) {
                    case 'q':
                        terminate();
                        break;
                    case 'r':
                        mode = REPLACE;
                        break;
                    case 'i':
                        mode = INSERT;
                        break;
                    case 'a':
                        mode = INSERT;
                        if (state.cursor < state.input_len) {
                            ++state.cursor;
                        }
                        break;
                    case 'I':
                        mode = INSERT;
                        state.cursor = 0;
                        break;
                    case 'A':
                        mode = INSERT;
                        state.cursor = state.input_len;
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (state.cursor < MAX_INPUT - 1 &&
                            state.cursor < state.input_len - 1) {
                            ++state.cursor;
                        }
                        break;
                    case 'w':
                        state.cursor = find_word_start(FALSE);
                        break;
                    case 'e':
                        state.cursor = find_word_end(FALSE);
                        break;
                    case 'b':
                        state.cursor = find_word_back(FALSE);
                        break;
                    case 'W':
                        state.cursor = find_word_start(TRUE);
                        break;
                    case 'E':
                        state.cursor = find_word_end(TRUE);
                        break;
                    case 'B':
                        state.cursor = find_word_back(TRUE);
                        break;
                    case '^':
                    case '_':
                        for (state.cursor = 0; state.cursor < state.input_len;
                             ++state.cursor) {
                            if (!isspace(state.input[state.cursor])) {
                                break;
                            }
                        }
                        break;
                    case '0':
                        state.cursor = 0;
                        break;
                    case '$':
                        state.cursor = state.input_len - 1;
                        break;
                    case 'D':
                        state.input_len = state.cursor;
                        push_history();
                        break;
                    case 'x':
                        if (state.input_len > 0) {
                            for (uint32_t i = state.cursor + 1;
                                 i < state.input_len; ++i) {
                                state.input[i - 1] = state.input[i];
                            }
                            --state.input_len;
                            if (state.cursor >= state.input_len &&
                                state.input_len > 0) {
                                state.cursor = state.input_len - 1;
                            }
                            push_history();
                        }
                        break;
                    case 'u':
                        undo_history();
                        break;
                    case CTRL('r'):
                        redo_history();
                        break;
                    default:
                        break;
                }
                break;

            case INSERT:
                switch (key) {
                    case K_ESCAPE:
                        mode = NORMAL;
                        if (state.cursor > 0) {
                            --state.cursor;
                        }
                        push_history();
                        break;
                    case K_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                        }
                        break;
                    case K_RIGHT:
                        if (state.cursor < MAX_INPUT &&
                            state.cursor < state.input_len) {
                            ++state.cursor;
                        }
                        break;
                    case K_BACKSPACE:
                        if (state.cursor > 0 && state.input_len > 0) {
                            for (uint32_t i = state.cursor; i < state.input_len;
                                 ++i) {
                                state.input[i - 1] = state.input[i];
                            }
                            --state.input_len;
                            --state.cursor;
                        }
                        break;
                    default:
                        if (isprint(key) && state.input_len < MAX_INPUT) {
                            for (uint32_t i = state.input_len;
                                 i >= state.cursor + 1; --i) {
                                state.input[i] = state.input[i - 1];
                            }
                            state.input[state.cursor] = key;
                            ++state.cursor;
                            ++state.input_len;
                        }
                        break;
                };
                break;

            case REPLACE:
                switch (key) {
                    case K_ESCAPE:
                        mode = NORMAL;
                        break;
                    default:
                        state.input[state.cursor] = key;
                        mode = NORMAL;
                        push_history();
                        break;
                }
                break;
        }
    }

    terminate();
    return 0;
}
