#include <ctype.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define CTRL(key) ((key) - 0x60)
#define K_ESCAPE (0x1b)
#define K_LEFT (0x104)
#define K_RIGHT (0x105)
#define K_BACKSPACE (0x107)
#define K_RETURN (0x0a)

#define MAX_INPUT (200)
#define MAX_HISTORY (100)

enum VimMode {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_REPLACE,
    MODE_VISUAL,
};

typedef struct State {
    // Not null-terminated
    char input[MAX_INPUT];
    uint32_t input_len;
    uint32_t cursor;
    uint32_t offset;
    uint32_t visual_start;
} State;

// TODO: Use cyclic array or heap allocate
typedef struct History {
    State states[MAX_HISTORY];
    uint32_t len;
    uint32_t index;
} History;

uint32_t INPUT_WIDTH = 20;
uint32_t BOX_Y = 0;
uint32_t BOX_X = 0;

enum VimMode MODE = MODE_NORMAL;

const uint32_t CURSOR_LEFT = 5;
const uint32_t CURSOR_RIGHT = 1;
const uint32_t MAX_INPUT_WIDTH = 70;
const uint32_t BOX_MARGIN = 2;

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
        case MODE_NORMAL:
            return "NORMAL";
        case MODE_INSERT:
            return "INSERT";
        case MODE_REPLACE:
            return "REPLACE";
        case MODE_VISUAL:
            return "VISUAL";
        default:
            return "?";
    }
}

void set_cursor(enum VimMode mode) {
    if (mode == MODE_INSERT) {
        printf("\033[5 q");
    } else {
        printf("\033[1 q");
    }
    fflush(stdout);
}

void draw_box_outline(
    uint32_t x, uint32_t y, uint32_t w, bool left_open, bool right_open
) {
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

int find_word_start(State* state, bool full_word) {
    // Empty line
    if (state->input_len < 1) {
        return 0;
    }
    // At end of line
    if (state->cursor + 1 >= state->input_len) {
        return state->input_len - 1;
    }
    // On a space
    // Look for first non-space character
    if (isspace(state->input[state->cursor])) {
        while (state->cursor + 1 < state->input_len) {
            ++state->cursor;
            if (!isspace(state->input[state->cursor])) {
                return state->cursor;
            }
        }
    }
    // On non-space
    int alnum = isalnum(state->input[state->cursor]);
    while (state->cursor < state->input_len - 1) {
        ++state->cursor;
        // Space found
        // Look for first non-space character
        if (isspace(state->input[state->cursor])) {
            while (state->cursor + 1 < state->input_len) {
                ++state->cursor;
                if (!isspace(state->input[state->cursor])) {
                    return state->cursor;
                }
            }
            break;
        }
        // First punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (!full_word && isalnum(state->input[state->cursor]) != alnum) {
            return state->cursor;
        }
    }
    // No next word found
    // Go to end of line
    return state->input_len - 1;
}

int find_word_end(State* state, bool full_word) {
    // Empty line
    if (state->input_len < 1) {
        return 0;
    }
    // At end of line
    if (state->cursor + 1 >= state->input_len) {
        return state->input_len - 1;
    }
    ++state->cursor;  // Always move at least one character
    // On a sequence of spaces (>=1)
    // Look for start of next word, start from there instead
    while (state->cursor + 1 < state->input_len &&
           isspace(state->input[state->cursor])) {
        ++state->cursor;
    }
    // On non-space
    int alnum = isalnum(state->input[state->cursor]);
    while (state->cursor < state->input_len) {
        ++state->cursor;
        // Space found
        // Word ends at previous index
        // OR first punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (isspace(state->input[state->cursor]) ||
            (!full_word && isalnum(state->input[state->cursor]) != alnum)) {
            return state->cursor - 1;
        }
    }
    // No next word found
    // Go to end of line
    return state->input_len - 1;
}

int find_word_back(State* state, bool full_word) {
    // At start of line
    if (state->cursor <= 1) {
        return 0;
    }
    // Start at previous character
    --state->cursor;
    // On a sequence of spaces (>=1)
    // Look for end of previous word, start from there instead
    while (state->cursor > 0 && isspace(state->input[state->cursor])) {
        --state->cursor;
    }
    // Now on a non-space
    int alnum = isalnum(state->input[state->cursor]);
    while (state->cursor > 0) {
        state->cursor--;
        // Space found
        // OR first punctuation before word
        // OR first word before punctuation
        // Word starts at next index
        // (If distinguishing words and punctuation)
        if (isspace(state->input[state->cursor]) ||
            (!full_word && isalnum(state->input[state->cursor]) != alnum)) {
            return state->cursor + 1;
        }
    }
    // No previous word found
    // Go to start of line
    return 0;
}

bool equals_state_input(const State* const s1, const State* const s2) {
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

void copy_state(const State* const src, State* const dest) {
    memcpy(dest, src, sizeof(State));
}

void push_history(State* state, History* history) {
    // Delete all future history to be overwritten
    if (history->index <= history->len) {
        history->len = history->index;
    }
    // Ignore if same as last entry
    if (history->len > 0 &&
        equals_state_input(state, &history->states[history->len - 1])) {
        return;
    }
    if (history->len >= MAX_HISTORY) {
        for (uint32_t i = 1; i < history->len; ++i) {
            copy_state(&history->states[i], &history->states[i - 1]);
        }
    } else {
        ++history->len;
        ++history->index;
    }

    copy_state(state, &history->states[history->index - 1]);
}

void undo_history(State* state, History* history) {
    if (history->len == 0 || history->index <= 0) {
        return;
    }
    --history->index;
    copy_state(&history->states[history->index], state);
}
void redo_history(State* state, History* history) {
    if (history->index + 1 >= history->len) {
        return;
    }
    ++history->index;
    copy_state(&history->states[history->index], state);
}

void save_input(State* state, const char* const filename) {
    // If no output file is specified, print instead
    if (filename == NULL) {
        for (uint32_t i = 0; i < state->input_len; ++i) {
            printf("%c", state->input[i]);
        }
        printf("\n");
        return;
    }

    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    for (uint32_t i = 0; i < state->input_len; ++i) {
        if (fprintf(file, "%c", state->input[i]) < 1) {
            perror("Failed to write file");
            exit(1);
        }
    }

    if (fclose(file) != 0) {
        perror("Failed to close file");
        exit(1);
    }
}

void terminate() {
    endwin();
    exit(0);
}

void update_offset_left(State* state) {
    if (state->cursor < state->offset + CURSOR_LEFT) {
        state->offset = subsat(state->cursor, CURSOR_LEFT);
    }
}
void update_offset_right(State* state) {
    if (state->cursor + CURSOR_RIGHT > INPUT_WIDTH) {
        state->offset = subsat(state->cursor + CURSOR_RIGHT, INPUT_WIDTH);
    }
}

bool in_visual_select(State* state, uint32_t index) {
    if (state->cursor == state->visual_start) {
        return index == state->visual_start;
    }
    if (state->cursor < state->visual_start) {
        return index >= state->cursor && index <= state->visual_start;
    }
    return index >= state->visual_start && index <= state->cursor;
}

int main(const int argc, const char* const* const argv) {
    if (argc > 2 || (argc > 1 && argv[1][0] == '-')) {
        fprintf(
            stderr,
            "USAGE:\n"
            "    vimput [FILENAME]\n"
            "\n"
            "ARGUMENTS:\n"
            "    [FILENAME] (optional)\n"
            "        Write the input to this file on <CR>\n"
        );
        return 1;
    }

    const char* const filename = argc > 1 ? argv[1] : NULL;

    State state = {
        .input = "abc def ghi jkl lmn opq rst uvw xyz",
        .input_len = 9 * 3 + 8,
        .cursor = 0,
        .offset = 0,
        .visual_start = 0,
    };
    History history = {
        .states = {{{0}}},
        .len = 0,
        .index = 0,
    };

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

    push_history(&state, &history);

    int key = 0;

    while (TRUE) {
        clear();

        int max_rows = getmaxy(stdscr);
        int max_cols = getmaxx(stdscr);

        INPUT_WIDTH = min(max_cols - BOX_MARGIN * 2 - 2, MAX_INPUT_WIDTH);
        BOX_X = (max_cols - INPUT_WIDTH) / 2 - 1;
        BOX_Y = max_rows / 2 - 1;

        attron(attr_box);
        draw_box_outline(
            BOX_X,
            BOX_Y,
            INPUT_WIDTH + 2,
            state.offset > 0,
            state.offset + INPUT_WIDTH < state.input_len
        );
        attroff(attr_box);

        move(BOX_Y + 1, BOX_X + 1);
        for (uint32_t i = 0; i < INPUT_WIDTH; ++i) {
            uint32_t index = i + state.offset;
            if (index < state.input_len) {
                if (MODE == MODE_VISUAL && in_visual_select(&state, index)) {
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
        printw("%8s", mode_name(MODE));
        printw(" [%3d /%3d]", state.cursor, state.input_len);
        printw(" [%3d /%3d]", history.index, history.len);
        printw(" 0x%02x", key);
        attroff(attr_details);

        set_cursor(MODE);
        move(BOX_Y + 1, BOX_X + subsat(state.cursor, state.offset) + 1);

        refresh();

        key = getch();

        switch (MODE) {
            case MODE_NORMAL:
                switch (key) {
                    case 'q':
                        /* case K_ESCAPE: */
                        endwin();
                        exit(0);
                        break;
                    case K_RETURN:
                        endwin();
                        save_input(&state, filename);
                        exit(0);
                        break;
                    case 'r':
                        MODE = MODE_REPLACE;
                        break;
                    case 'v':
                        MODE = MODE_VISUAL;
                        state.visual_start = state.cursor;
                        break;
                    case 'V':
                        MODE = MODE_VISUAL;
                        state.visual_start = 0;
                        state.cursor = state.input_len - 1;
                        break;
                    case 'i':
                        MODE = MODE_INSERT;
                        break;
                    case 'a':
                        MODE = MODE_INSERT;
                        if (state.cursor < state.input_len) {
                            ++state.cursor;
                        }
                        break;
                    case 'I':
                        MODE = MODE_INSERT;
                        state.cursor = 0;
                        state.offset = 0;
                        break;
                    case 'A':
                        MODE = MODE_INSERT;
                        state.cursor = state.input_len;
                        state.offset = subsat(state.cursor + 1, INPUT_WIDTH);
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left(&state);
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (state.cursor < MAX_INPUT - 1 &&
                            state.cursor < state.input_len - 1) {
                            ++state.cursor;
                            update_offset_right(&state);
                        }
                        break;
                    case 'w':
                        state.cursor = find_word_start(&state, FALSE);
                        update_offset_right(&state);
                        break;
                    case 'e':
                        state.cursor = find_word_end(&state, FALSE);
                        update_offset_right(&state);
                        break;
                    case 'b':
                        state.cursor = find_word_back(&state, FALSE);
                        update_offset_left(&state);
                        break;
                    case 'W':
                        state.cursor = find_word_start(&state, TRUE);
                        update_offset_right(&state);
                        break;
                    case 'E':
                        state.cursor = find_word_end(&state, TRUE);
                        update_offset_right(&state);
                        break;
                    case 'B':
                        state.cursor = find_word_back(&state, TRUE);
                        update_offset_left(&state);
                        break;
                    case '^':
                    case '_':
                        for (state.cursor = 0; state.cursor < state.input_len;
                             ++state.cursor) {
                            if (!isspace(state.input[state.cursor])) {
                                break;
                            }
                        }
                        update_offset_left(&state);
                        break;
                    case '0':
                        state.cursor = 0;
                        state.offset = 0;
                        break;
                    case '$':
                        state.cursor = state.input_len - 1;
                        state.offset = subsat(state.cursor + 2, INPUT_WIDTH);
                        break;
                    case 'D':
                        state.input_len = state.cursor;
                        push_history(&state, &history);
                        break;
                    case 'x':
                        if (state.input_len > 0) {
                            for (uint32_t i = state.cursor + 1;
                                 i < state.input_len;
                                 ++i) {
                                state.input[i - 1] = state.input[i];
                            }
                            --state.input_len;
                            if (state.cursor >= state.input_len &&
                                state.input_len > 0) {
                                state.cursor = state.input_len - 1;
                            }
                            update_offset_left(&state);
                            push_history(&state, &history);
                        }
                        break;
                    case 'u':
                        undo_history(&state, &history);
                        break;
                    case CTRL('r'):
                        redo_history(&state, &history);
                        break;
                    default:
                        break;
                }
                break;

            case MODE_INSERT:
                switch (key) {
                    case K_ESCAPE:
                        MODE = MODE_NORMAL;
                        if (state.cursor > 0) {
                            --state.cursor;
                        }
                        push_history(&state, &history);
                        break;
                    case K_RETURN:
                        endwin();
                        save_input(&state, filename);
                        exit(0);
                        break;
                    case K_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left(&state);
                        }
                        break;
                    case K_RIGHT:
                        if (state.cursor < MAX_INPUT &&
                            state.cursor < state.input_len) {
                            ++state.cursor;
                            update_offset_right(&state);
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
                            update_offset_left(&state);
                        }
                        break;
                    default:
                        if (isprint(key) && state.input_len < MAX_INPUT) {
                            for (uint32_t i = state.input_len;
                                 i >= state.cursor + 1;
                                 --i) {
                                state.input[i] = state.input[i - 1];
                            }
                            state.input[state.cursor] = key;
                            ++state.cursor;
                            ++state.input_len;
                            update_offset_right(&state);
                        }
                        break;
                };
                break;

            case MODE_REPLACE:
                switch (key) {
                    case K_ESCAPE:
                        MODE = MODE_NORMAL;
                        break;
                    default:
                        if (isprint(key)) {
                            state.input[state.cursor] = key;
                            MODE = MODE_NORMAL;
                            push_history(&state, &history);
                        }
                        break;
                }
                break;

            case MODE_VISUAL: {
                uint32_t start = min(state.cursor, state.visual_start);
                uint32_t size =
                    difference(state.cursor, state.visual_start) + 1;
                switch (key) {
                    case K_ESCAPE:
                        MODE = MODE_NORMAL;
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (state.cursor > 0) {
                            --state.cursor;
                            update_offset_left(&state);
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (state.cursor < MAX_INPUT - 1 &&
                            state.cursor < state.input_len - 1) {
                            ++state.cursor;
                            update_offset_right(&state);
                        }
                        break;
                    case 'w':
                        state.cursor = find_word_start(&state, FALSE);
                        update_offset_right(&state);
                        break;
                    case 'e':
                        state.cursor = find_word_end(&state, FALSE);
                        update_offset_right(&state);
                        break;
                    case 'b':
                        state.cursor = find_word_back(&state, FALSE);
                        update_offset_left(&state);
                        break;
                    case 'W':
                        state.cursor = find_word_start(&state, TRUE);
                        update_offset_right(&state);
                        break;
                    case 'E':
                        state.cursor = find_word_end(&state, TRUE);
                        update_offset_right(&state);
                        break;
                    case 'B':
                        state.cursor = find_word_back(&state, TRUE);
                        update_offset_left(&state);
                        break;
                    case '^':
                    case '_':
                        for (state.cursor = 0; state.cursor < state.input_len;
                             ++state.cursor) {
                            if (!isspace(state.input[state.cursor])) {
                                break;
                            }
                        }
                        update_offset_left(&state);
                        break;
                    case '0':
                        state.cursor = 0;
                        state.offset = 0;
                        break;
                    case '$':
                        state.cursor = state.input_len - 1;
                        state.offset = subsat(state.cursor + 2, INPUT_WIDTH);
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
                        if (state.cursor > state.visual_start) {
                            state.cursor -= size - 1;
                        }
                        if (state.cursor + 1 >= state.input_len) {
                            state.cursor = subsat(state.input_len, 1);
                        }
                        MODE = MODE_NORMAL;
                        push_history(&state, &history);
                    } break;
                    case 'u': {
                        for (uint32_t i = 0; i < size; ++i) {
                            state.input[start + i] =
                                tolower(state.input[start + i]);
                        }
                        if (state.cursor > state.visual_start) {
                            state.cursor -= size - 1;
                        }
                        MODE = MODE_NORMAL;
                        push_history(&state, &history);
                    }; break;
                    case 'U': {
                        for (uint32_t i = 0; i < size; ++i) {
                            state.input[start + i] =
                                toupper(state.input[start + i]);
                        }
                        if (state.cursor > state.visual_start) {
                            state.cursor -= size - 1;
                        }
                        MODE = MODE_NORMAL;
                        push_history(&state, &history);
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
