#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define CTRL(key) key - 0x60
#define K_ESCAPE 0x1b
#define K_LEFT 0x104
#define K_RIGHT 0x105
#define K_BACKSPACE 0x107
#define K_RETURN 0x0a

#define MAX_INPUT 200
#define MAX_HISTORY 100

enum VimMode {
    NORMAL,
    INSERT,
    REPLACE,
    VISUAL,
};

typedef struct State {
    // Not null-terminated
    char input[MAX_INPUT];
    uint32_t input_len;
    uint32_t cursor;
    uint32_t offset;
} State;

// TODO: Use cyclic array or heap allocate
typedef struct History {
    State states[MAX_HISTORY];
    uint32_t len;
    uint32_t index;
} History;

uint32_t input_width = 20;
uint32_t box_y = 0;
uint32_t box_x = 0;

enum VimMode mode = NORMAL;
uint32_t visual_start = 0;

State state = {
    .input = "abc def ghi jkl",
    .input_len = 15,
    .cursor = 0,
    .offset = 0,
};

History history = {
    .states = {{{0}}},
    .len = 0,
    .index = 0,
};

// TODO: Use macros
const uint32_t cursor_left = 5;
const uint32_t cursor_right = 1;
const uint32_t max_input_width = 70;
const uint32_t box_margin = 2;

uint32_t subsat(uint32_t lhs, uint32_t rhs) {
    if (rhs >= lhs) {
        return 0;
    }
    return lhs - rhs;
}
uint32_t min(uint32_t lhs, uint32_t rhs) {
    if (rhs >= lhs) {
        return lhs;
    }
    return rhs;
}
uint32_t difference(uint32_t lhs, uint32_t rhs) {
    if (lhs >= rhs) {
        return lhs - rhs;
    }
    return rhs - lhs;
}

const char* mode_name(enum VimMode mode) {
    switch (mode) {
        case NORMAL:
            return "NORMAL";
        case INSERT:
            return "INSERT";
        case REPLACE:
            return "REPLACE";
        case VISUAL:
            return "VISUAL";
        default:
            return "?";
    }
}

void set_cursor(enum VimMode mode) {
    if (mode == INSERT) {
        printf("\033[5 q");
    } else {
        printf("\033[1 q");
    }
    fflush(stdout);
}

void draw_box_outline(uint32_t x, uint32_t y, uint32_t w, bool left_open,
                      bool right_open) {
    // Top
    move(y, x);
    addch(ACS_ULCORNER);
    for (uint32_t i = 0; i < w - 2; ++i) {
        addch(ACS_HLINE);
    }
    addch(ACS_URCORNER);

    // Sides
    move(y + 1, x);
    addch(left_open ? ':' : ACS_VLINE);
    move(y + 1, x + w - 1);
    addch(right_open ? ':' : ACS_VLINE);

    // Bottom
    move(y + 2, x);
    addch(ACS_LLCORNER);
    for (uint32_t i = 0; i < w - 2; ++i) {
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
    while (state.cursor < state.input_len - 1) {
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

void copy_state(State* src, State* dest) { memcpy(dest, src, sizeof(State)); }

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
    if (history.len >= MAX_HISTORY) {
        for (uint32_t i = 1; i < history.len; ++i) {
            copy_state(&history.states[i], &history.states[i - 1]);
        }
    } else {
        ++history.len;
        ++history.index;
    }

    copy_state(&state, &history.states[history.index - 1]);
}

void undo_history() {
    if (history.len == 0 || history.index <= 0) {
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

void print_input() {
    for (uint32_t i = 0; i < state.input_len; ++i) {
        printf("%c", state.input[i]);
    }
    printf("\n");
}

void terminate() {
    endwin();
    exit(0);
}

void update_offset_left() {
    if (state.cursor < state.offset + cursor_left) {
        state.offset = subsat(state.cursor, cursor_left);
    }
}
void update_offset_right() {
    if (state.cursor + cursor_right > input_width) {
        state.offset = subsat(state.cursor + cursor_right, input_width);
    }
}

bool in_visual_select(uint32_t index) {
    if (state.cursor == visual_start) {
        return index == visual_start;
    }
    if (state.cursor < visual_start) {
        return index >= state.cursor && index <= visual_start;
    }
    return index >= visual_start && index <= state.cursor;
}

int main() {
    initscr();
    noecho();              // Disable echoing
    cbreak();              // Disable line buffering
    keypad(stdscr, TRUE);  // Enable raw key input
    set_escdelay(0);       // Disable Escape key delay

    signal(SIGINT, terminate);  // Clean up on SIGINT

    start_color();         // Enable color
    use_default_colors();  // Don't change the background color

    init_pair(1, COLOR_BLUE, -1);
    init_pair(2, COLOR_WHITE, -1);
    init_pair(3, -1, COLOR_BLUE);
    const int attr_box = COLOR_PAIR(1) | A_DIM;
    const int attr_details = COLOR_PAIR(2) | A_DIM;
    const int attr_visual = COLOR_PAIR(3);

    push_history();

    int key = 0;

    while (TRUE) {
        clear();

        int max_rows = getmaxy(stdscr);
        int max_cols = getmaxx(stdscr);

        input_width = min(max_cols - box_margin * 2 - 2, max_input_width);
        box_x = (max_cols - input_width) / 2 - 1;
        box_y = max_rows / 2 - 1;

        attron(attr_box);
        draw_box_outline(box_x, box_y, input_width + 2, state.offset > 0,
                         state.offset + input_width < state.input_len);
        attroff(attr_box);

        move(box_y + 1, box_x + 1);
        for (uint32_t i = 0; i < input_width; ++i) {
            uint32_t index = i + state.offset;
            if (index < state.input_len) {
                if (mode == VISUAL && in_visual_select(index)) {
                    attron(attr_visual);
                }
                printw("%c", state.input[index]);
                attroff(attr_visual);
            } else {
                printw(" ");
            }
        }

        move(max_rows - 1, 0);
        attron(attr_details);
        printw("%8s", mode_name(mode));
        printw(" [%3d /%3d]", state.cursor, state.input_len);
        printw(" [%3d /%3d]", history.index, history.len);
        printw(" 0x%02x", key);
        attroff(attr_details);

        set_cursor(mode);
        move(box_y + 1, box_x + subsat(state.cursor, state.offset) + 1);

        refresh();

        key = getch();

        switch (mode) {
            case NORMAL:
                switch (key) {
                    case 'q':
                        /* case K_ESCAPE: */
                        endwin();
                        exit(0);
                        break;
                    case K_RETURN:
                        endwin();
                        print_input();
                        exit(0);
                        break;
                    case 'r':
                        mode = REPLACE;
                        break;
                    case 'v':
                        mode = VISUAL;
                        visual_start = state.cursor;
                        break;
                    case 'V':
                        mode = VISUAL;
                        visual_start = 0;
                        state.cursor = state.input_len - 1;
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
                        state.offset = 0;
                        break;
                    case 'A':
                        mode = INSERT;
                        state.cursor = state.input_len;
                        state.offset = subsat(state.cursor + 1, input_width);
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left();
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (state.cursor < MAX_INPUT - 1 &&
                            state.cursor < state.input_len - 1) {
                            ++state.cursor;
                            update_offset_right();
                        }
                        break;
                    case 'w':
                        state.cursor = find_word_start(FALSE);
                        update_offset_right();
                        break;
                    case 'e':
                        state.cursor = find_word_end(FALSE);
                        update_offset_right();
                        break;
                    case 'b':
                        state.cursor = find_word_back(FALSE);
                        update_offset_left();
                        break;
                    case 'W':
                        state.cursor = find_word_start(TRUE);
                        update_offset_right();
                        break;
                    case 'E':
                        state.cursor = find_word_end(TRUE);
                        update_offset_right();
                        break;
                    case 'B':
                        state.cursor = find_word_back(TRUE);
                        update_offset_left();
                        break;
                    case '^':
                    case '_':
                        for (state.cursor = 0; state.cursor < state.input_len;
                             ++state.cursor) {
                            if (!isspace(state.input[state.cursor])) {
                                break;
                            }
                        }
                        update_offset_left();
                        break;
                    case '0':
                        state.cursor = 0;
                        state.offset = 0;
                        break;
                    case '$':
                        state.cursor = state.input_len - 1;
                        state.offset = subsat(state.cursor + 2, input_width);
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
                            update_offset_left();
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
                    case K_RETURN:
                        endwin();
                        print_input();
                        exit(0);
                        break;
                    case K_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left();
                        }
                        break;
                    case K_RIGHT:
                        if (state.cursor < MAX_INPUT &&
                            state.cursor < state.input_len) {
                            ++state.cursor;
                            update_offset_right();
                        }
                        break;
                    case K_BACKSPACE:
                        if (state.cursor > 0 && state.input_len > 0) {
                            for (uint32_t i = state.cursor; i < state.input_len;
                                 ++i) {
                                state.input[i - 1] = state.input[i];
                            }
                            for (uint32_t i = state.input_len; i < MAX_INPUT;
                                 ++i) {
                                state.input[i] = '.';
                            }
                            --state.input_len;
                            --state.cursor;
                            update_offset_left();
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
                            update_offset_right();
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
                        if (isprint(key)) {
                            state.input[state.cursor] = key;
                            mode = NORMAL;
                            push_history();
                        }
                        break;
                }
                break;

            case VISUAL: {
                uint32_t start = min(state.cursor, visual_start);
                uint32_t size = difference(state.cursor, visual_start) + 1;
                switch (key) {
                    case K_ESCAPE:
                        mode = NORMAL;
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left();
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (state.cursor < MAX_INPUT - 1 &&
                            state.cursor < state.input_len - 1) {
                            ++state.cursor;
                            update_offset_right();
                        }
                        break;
                    case 'w':
                        state.cursor = find_word_start(FALSE);
                        update_offset_right();
                        break;
                    case 'e':
                        state.cursor = find_word_end(FALSE);
                        update_offset_right();
                        break;
                    case 'b':
                        state.cursor = find_word_back(FALSE);
                        update_offset_left();
                        break;
                    case 'W':
                        state.cursor = find_word_start(TRUE);
                        update_offset_right();
                        break;
                    case 'E':
                        state.cursor = find_word_end(TRUE);
                        update_offset_right();
                        break;
                    case 'B':
                        state.cursor = find_word_back(TRUE);
                        update_offset_left();
                        break;
                    case '^':
                    case '_':
                        for (state.cursor = 0; state.cursor < state.input_len;
                             ++state.cursor) {
                            if (!isspace(state.input[state.cursor])) {
                                break;
                            }
                        }
                        update_offset_left();
                        break;
                    case '0':
                        state.cursor = 0;
                        state.offset = 0;
                        break;
                    case '$':
                        state.cursor = state.input_len - 1;
                        state.offset = subsat(state.cursor + 2, input_width);
                        break;
                    case 'd':
                    case 'x': {
                        for (uint32_t i = start; i <= state.input_len - size;
                             ++i) {
                            uint32_t new = i + size;
                            if (new >= state.input_len) {
                                break;
                            }
                            state.input[i] = state.input[new];
                        }
                        state.input_len -= size;
                        if (state.cursor > visual_start) {
                            state.cursor -= size - 1;
                        }
                        if (state.cursor + 1 >= state.input_len) {
                            state.cursor = subsat(state.input_len, 1);
                        }
                        mode = NORMAL;
                        push_history();
                    } break;
                    case 'u': {
                        for (uint32_t i = 0; i < size; ++i) {
                            state.input[start + i] =
                                tolower(state.input[start + i]);
                        }
                        if (state.cursor > visual_start) {
                            state.cursor -= size - 1;
                        }
                        mode = NORMAL;
                        push_history();
                    }; break;
                    case 'U': {
                        for (uint32_t i = 0; i < size; ++i) {
                            state.input[start + i] =
                                toupper(state.input[start + i]);
                        }
                        if (state.cursor > visual_start) {
                            state.cursor -= size - 1;
                        }
                        mode = NORMAL;
                        push_history();
                    }; break;
                    default:
                        break;
                }
            } break;
        }
    }

    endwin();
    return 0;
}
