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

typedef struct Snap {
    // Not null-terminated
    char input[MAX_INPUT];
    uint32_t input_len;
    uint32_t cursor;
    uint32_t offset;
} Snap;

// TODO: Use cyclic array or heap allocate
typedef struct History {
    Snap snaps[MAX_HISTORY];
    uint32_t len;
    uint32_t index;
} History;

typedef struct State {
    enum VimMode mode;
    Snap snap;
    uint32_t visual_start;
    History history;
    const char* filename;
} State;

static struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
} input_box = {.x = 0, .y = 0, .width = 20};

const uint32_t CURSOR_LEFT = 5;
const uint32_t CURSOR_RIGHT = 1;
const uint32_t MAX_INPUT_WIDTH = 70;
const uint32_t BOX_MARGIN = 2;

const int PAIR_BOX = 1;
const int PAIR_DETAILS = 2;
const int PAIR_VISUAL = 3;
const int ATTR_BOX = COLOR_PAIR(PAIR_BOX) | A_DIM;
const int ATTR_DETAILS = COLOR_PAIR(PAIR_DETAILS) | A_DIM;
const int ATTR_VISUAL = COLOR_PAIR(PAIR_VISUAL);

uint32_t subsat(const uint32_t lhs, const uint32_t rhs) {
    if (rhs >= lhs) {
        return 0;
    }
    return lhs - rhs;
}
uint32_t min(const uint32_t lhs, const uint32_t rhs) {
    if (rhs >= lhs) {
        return lhs;
    }
    return rhs;
}
uint32_t difference(const uint32_t lhs, const uint32_t rhs) {
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

void update_input_box(const int max_rows, const int max_cols) {
    input_box.width = min(max_cols - BOX_MARGIN * 2 - 2, MAX_INPUT_WIDTH);
    input_box.x = (max_cols - input_box.width) / 2 - 1;
    input_box.y = max_rows / 2 - 1;
}

void draw_box_outline(
    const uint32_t x,
    const uint32_t y,
    const uint32_t w,
    const bool left_open,
    const bool right_open
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

int find_word_start(Snap* const snap, const bool full_word) {
    // Empty line
    if (snap->input_len < 1) {
        return 0;
    }
    // At end of line
    if (snap->cursor + 1 >= snap->input_len) {
        return snap->input_len - 1;
    }
    // On a space
    // Look for first non-space character
    if (isspace(snap->input[snap->cursor])) {
        while (snap->cursor + 1 < snap->input_len) {
            ++snap->cursor;
            if (!isspace(snap->input[snap->cursor])) {
                return snap->cursor;
            }
        }
    }
    // On non-space
    int alnum = isalnum(snap->input[snap->cursor]);
    while (snap->cursor < snap->input_len - 1) {
        ++snap->cursor;
        // Space found
        // Look for first non-space character
        if (isspace(snap->input[snap->cursor])) {
            while (snap->cursor + 1 < snap->input_len) {
                ++snap->cursor;
                if (!isspace(snap->input[snap->cursor])) {
                    return snap->cursor;
                }
            }
            break;
        }
        // First punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (!full_word && isalnum(snap->input[snap->cursor]) != alnum) {
            return snap->cursor;
        }
    }
    // No next word found
    // Go to end of line
    return snap->input_len - 1;
}

int find_word_end(Snap* const snap, const bool full_word) {
    // Empty line
    if (snap->input_len < 1) {
        return 0;
    }
    // At end of line
    if (snap->cursor + 1 >= snap->input_len) {
        return snap->input_len - 1;
    }
    ++snap->cursor;  // Always move at least one character
    // On a sequence of spaces (>=1)
    // Look for start of next word, start from there instead
    while (snap->cursor + 1 < snap->input_len &&
           isspace(snap->input[snap->cursor])) {
        ++snap->cursor;
    }
    // On non-space
    int alnum = isalnum(snap->input[snap->cursor]);
    while (snap->cursor < snap->input_len) {
        ++snap->cursor;
        // Space found
        // Word ends at previous index
        // OR first punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (isspace(snap->input[snap->cursor]) ||
            (!full_word && isalnum(snap->input[snap->cursor]) != alnum)) {
            return snap->cursor - 1;
        }
    }
    // No next word found
    // Go to end of line
    return snap->input_len - 1;
}

int find_word_back(Snap* const snap, const bool full_word) {
    // At start of line
    if (snap->cursor <= 1) {
        return 0;
    }
    // Start at previous character
    --snap->cursor;
    // On a sequence of spaces (>=1)
    // Look for end of previous word, start from there instead
    while (snap->cursor > 0 && isspace(snap->input[snap->cursor])) {
        --snap->cursor;
    }
    // Now on a non-space
    int alnum = isalnum(snap->input[snap->cursor]);
    while (snap->cursor > 0) {
        snap->cursor--;
        // Space found
        // OR first punctuation before word
        // OR first word before punctuation
        // Word starts at next index
        // (If distinguishing words and punctuation)
        if (isspace(snap->input[snap->cursor]) ||
            (!full_word && isalnum(snap->input[snap->cursor]) != alnum)) {
            return snap->cursor + 1;
        }
    }
    // No previous word found
    // Go to start of line
    return 0;
}

bool equals_snap_input(const Snap* const a, const Snap* const b) {
    if (a->input_len != b->input_len) {
        return false;
    }
    for (uint32_t i = 0; i < a->input_len; ++i) {
        if (a->input[i] != b->input[i]) {
            return false;
        }
    }
    return true;
}

void copy_snap(const Snap* const src, Snap* const dest) {
    memcpy(dest, src, sizeof(Snap));
}

void push_history(State* const state) {
    // Delete all future history to be overwritten
    if (state->history.index <= state->history.len) {
        state->history.len = state->history.index;
    }
    // Ignore if same as last entry
    if (state->history.len > 0 &&
        equals_snap_input(
            &state->snap, &state->history.snaps[state->history.len - 1]
        )) {
        return;
    }
    if (state->history.len >= MAX_HISTORY) {
        for (uint32_t i = 1; i < state->history.len; ++i) {
            copy_snap(&state->history.snaps[i], &state->history.snaps[i - 1]);
        }
    } else {
        ++state->history.len;
        ++state->history.index;
    }

    copy_snap(&state->snap, &state->history.snaps[state->history.index - 1]);
}

void undo_history(State* const state) {
    if (state->history.len == 0 || state->history.index <= 0) {
        return;
    }
    --state->history.index;
    copy_snap(&state->history.snaps[state->history.index], &state->snap);
}
void redo_history(State* const state) {
    if (state->history.index + 1 >= state->history.len) {
        return;
    }
    ++state->history.index;
    copy_snap(&state->history.snaps[state->history.index], &state->snap);
}

void save_input(const State* const state) {
    // If no output file is specified, print instead
    if (state->filename == NULL) {
        for (uint32_t i = 0; i < state->snap.input_len; ++i) {
            printf("%c", state->snap.input[i]);
        }
        printf("\n");
        return;
    }

    FILE* file = fopen(state->filename, "w");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    for (uint32_t i = 0; i < state->snap.input_len; ++i) {
        if (fprintf(file, "%c", state->snap.input[i]) < 1) {
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

void update_offset_left(Snap* const snap) {
    if (snap->cursor < snap->offset + CURSOR_LEFT) {
        snap->offset = subsat(snap->cursor, CURSOR_LEFT);
    }
}
void update_offset_right(Snap* const snap, const uint32_t width) {
    if (snap->cursor + CURSOR_RIGHT > width) {
        snap->offset = subsat(snap->cursor + CURSOR_RIGHT, width);
    }
}

bool in_visual_select(const State* const state, const uint32_t index) {
    if (state->snap.cursor == state->visual_start) {
        return index == state->visual_start;
    }
    if (state->snap.cursor < state->visual_start) {
        return index >= state->snap.cursor && index <= state->visual_start;
    }
    return index >= state->visual_start && index <= state->snap.cursor;
}

void frame(State* const state, int* const key) {
    clear();

    int max_rows = getmaxy(stdscr);
    int max_cols = getmaxx(stdscr);
    update_input_box(max_rows, max_cols);

    attron(ATTR_BOX);
    draw_box_outline(
        input_box.x,
        input_box.y,
        input_box.width + 2,
        state->snap.offset > 0,
        state->snap.offset + input_box.width < state->snap.input_len
    );
    attroff(ATTR_BOX);

    move(input_box.y + 1, input_box.x + 1);
    for (uint32_t i = 0; i < input_box.width; ++i) {
        uint32_t index = i + state->snap.offset;
        if (index < state->snap.input_len) {
            if (state->mode == MODE_VISUAL && in_visual_select(state, index)) {
                attron(ATTR_VISUAL);
            }
            printw("%c", state->snap.input[index]);
            attroff(ATTR_VISUAL);
        } else {
            printw(" ");
        }
    }

    move(max_rows - 1, 0);
    attron(ATTR_DETAILS);
    printw("%8s", mode_name(state->mode));
    printw(" [%3d /%3d]", state->snap.cursor, state->snap.input_len);
    printw(" [%3d /%3d]", state->history.index, state->history.len);
    printw(" 0x%02x", *key);
    attroff(ATTR_DETAILS);

    set_cursor(state->mode);
    move(
        input_box.y + 1,
        input_box.x + subsat(state->snap.cursor, state->snap.offset) + 1
    );

    refresh();

    *key = getch();

    switch (state->mode) {
        case MODE_NORMAL:
            switch (*key) {
                case 'q':
                    endwin();
                    exit(0);
                    break;
                case K_RETURN:
                    endwin();
                    save_input(state);
                    exit(0);
                    break;
                case 'r':
                    state->mode = MODE_REPLACE;
                    break;
                case 'v':
                    state->mode = MODE_VISUAL;
                    state->visual_start = state->snap.cursor;
                    break;
                case 'V':
                    state->mode = MODE_VISUAL;
                    state->visual_start = 0;
                    state->snap.cursor = state->snap.input_len - 1;
                    break;
                case 'i':
                    state->mode = MODE_INSERT;
                    break;
                case 'a':
                    state->mode = MODE_INSERT;
                    if (state->snap.cursor < state->snap.input_len) {
                        ++state->snap.cursor;
                    }
                    break;
                case 'I':
                    state->mode = MODE_INSERT;
                    state->snap.cursor = 0;
                    state->snap.offset = 0;
                    break;
                case 'A':
                    state->mode = MODE_INSERT;
                    state->snap.cursor = state->snap.input_len;
                    state->snap.offset =
                        subsat(state->snap.cursor + 1, input_box.width);
                    break;
                case 'h':
                case KEY_LEFT:
                    if (state->snap.cursor > 0) {
                        --state->snap.cursor;
                        update_offset_left(&state->snap);
                    }
                    break;
                case 'l':
                case KEY_RIGHT:
                    if (state->snap.cursor < MAX_INPUT - 1 &&
                        state->snap.cursor < state->snap.input_len - 1) {
                        ++state->snap.cursor;
                        update_offset_right(&state->snap, input_box.width);
                    }
                    break;
                case 'w':
                    state->snap.cursor = find_word_start(&state->snap, FALSE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'e':
                    state->snap.cursor = find_word_end(&state->snap, FALSE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'b':
                    state->snap.cursor = find_word_back(&state->snap, FALSE);
                    update_offset_left(&state->snap);
                    break;
                case 'W':
                    state->snap.cursor = find_word_start(&state->snap, TRUE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'E':
                    state->snap.cursor = find_word_end(&state->snap, TRUE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'B':
                    state->snap.cursor = find_word_back(&state->snap, TRUE);
                    update_offset_left(&state->snap);
                    break;
                case '^':
                case '_':
                    for (state->snap.cursor = 0;
                         state->snap.cursor < state->snap.input_len;
                         ++state->snap.cursor) {
                        if (!isspace(state->snap.input[state->snap.cursor])) {
                            break;
                        }
                    }
                    update_offset_left(&state->snap);
                    break;
                case '0':
                    state->snap.cursor = 0;
                    state->snap.offset = 0;
                    break;
                case '$':
                    state->snap.cursor = state->snap.input_len - 1;
                    state->snap.offset =
                        subsat(state->snap.cursor + 2, input_box.width);
                    break;
                case 'D':
                    state->snap.input_len = state->snap.cursor;
                    push_history(state);
                    break;
                case 'x':
                    if (state->snap.input_len > 0) {
                        for (uint32_t i = state->snap.cursor + 1;
                             i < state->snap.input_len;
                             ++i) {
                            state->snap.input[i - 1] = state->snap.input[i];
                        }
                        --state->snap.input_len;
                        if (state->snap.cursor >= state->snap.input_len &&
                            state->snap.input_len > 0) {
                            state->snap.cursor = state->snap.input_len - 1;
                        }
                        update_offset_left(&state->snap);
                        push_history(state);
                    }
                    break;
                case 'u':
                    undo_history(state);
                    break;
                case CTRL('r'):
                    redo_history(state);
                    break;
                default:
                    break;
            }
            break;

        case MODE_INSERT:
            switch (*key) {
                case K_ESCAPE:
                    state->mode = MODE_NORMAL;
                    if (state->snap.cursor > 0) {
                        --state->snap.cursor;
                    }
                    push_history(state);
                    break;
                case K_RETURN:
                    endwin();
                    save_input(state);
                    exit(0);
                    break;
                case K_LEFT:
                    if (state->snap.cursor > 0) {
                        --state->snap.cursor;
                        update_offset_left(&state->snap);
                    }
                    break;
                case K_RIGHT:
                    if (state->snap.cursor < MAX_INPUT &&
                        state->snap.cursor < state->snap.input_len) {
                        ++state->snap.cursor;
                        update_offset_right(&state->snap, input_box.width);
                    }
                    break;
                case K_BACKSPACE:
                    if (state->snap.cursor > 0 && state->snap.input_len > 0) {
                        for (uint32_t i = state->snap.cursor;
                             i < state->snap.input_len;
                             ++i) {
                            state->snap.input[i - 1] = state->snap.input[i];
                        }
                        for (uint32_t i = state->snap.input_len; i < MAX_INPUT;
                             ++i) {
                            state->snap.input[i] = '.';
                        }
                        --state->snap.input_len;
                        --state->snap.cursor;
                        update_offset_left(&state->snap);
                    }
                    break;
                default:
                    if (isprint(*key) && state->snap.input_len < MAX_INPUT) {
                        for (uint32_t i = state->snap.input_len;
                             i >= state->snap.cursor + 1;
                             --i) {
                            state->snap.input[i] = state->snap.input[i - 1];
                        }
                        state->snap.input[state->snap.cursor] = *key;
                        ++state->snap.cursor;
                        ++state->snap.input_len;
                        update_offset_right(&state->snap, input_box.width);
                    }
                    break;
            };
            break;

        case MODE_REPLACE:
            switch (*key) {
                case K_ESCAPE:
                    state->mode = MODE_NORMAL;
                    break;
                default:
                    if (isprint(*key)) {
                        state->snap.input[state->snap.cursor] = *key;
                        state->mode = MODE_NORMAL;
                        push_history(state);
                    }
                    break;
            }
            break;

        case MODE_VISUAL: {
            uint32_t start = min(state->snap.cursor, state->visual_start);
            uint32_t size =
                difference(state->snap.cursor, state->visual_start) + 1;
            switch (*key) {
                case K_ESCAPE:
                    state->mode = MODE_NORMAL;
                    break;
                case 'h':
                case KEY_LEFT:
                    if (state->snap.cursor > 0) {
                        --state->snap.cursor;
                        update_offset_left(&state->snap);
                    }
                    break;
                case 'l':
                case KEY_RIGHT:
                    if (state->snap.cursor < MAX_INPUT - 1 &&
                        state->snap.cursor < state->snap.input_len - 1) {
                        ++state->snap.cursor;
                        update_offset_right(&state->snap, input_box.width);
                    }
                    break;
                case 'w':
                    state->snap.cursor = find_word_start(&state->snap, FALSE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'e':
                    state->snap.cursor = find_word_end(&state->snap, FALSE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'b':
                    state->snap.cursor = find_word_back(&state->snap, FALSE);
                    update_offset_left(&state->snap);
                    break;
                case 'W':
                    state->snap.cursor = find_word_start(&state->snap, TRUE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'E':
                    state->snap.cursor = find_word_end(&state->snap, TRUE);
                    update_offset_right(&state->snap, input_box.width);
                    break;
                case 'B':
                    state->snap.cursor = find_word_back(&state->snap, TRUE);
                    update_offset_left(&state->snap);
                    break;
                case '^':
                case '_':
                    for (state->snap.cursor = 0;
                         state->snap.cursor < state->snap.input_len;
                         ++state->snap.cursor) {
                        if (!isspace(state->snap.input[state->snap.cursor])) {
                            break;
                        }
                    }
                    update_offset_left(&state->snap);
                    break;
                case '0':
                    state->snap.cursor = 0;
                    state->snap.offset = 0;
                    break;
                case '$':
                    state->snap.cursor = state->snap.input_len - 1;
                    state->snap.offset =
                        subsat(state->snap.cursor + 2, input_box.width);
                    break;
                case 'd':
                case 'x': {
                    for (uint32_t i = start; i <= state->snap.input_len - size;
                         ++i) {
                        uint32_t new = i + size;
                        if (new >= state->snap.input_len) {
                            break;
                        }
                        state->snap.input[i] = state->snap.input[new];
                    }
                    state->snap.input_len -= size;
                    if (state->snap.cursor > state->visual_start) {
                        state->snap.cursor -= size - 1;
                    }
                    if (state->snap.cursor + 1 >= state->snap.input_len) {
                        state->snap.cursor = subsat(state->snap.input_len, 1);
                    }
                    state->mode = MODE_NORMAL;
                    push_history(state);
                } break;
                case 'u': {
                    for (uint32_t i = 0; i < size; ++i) {
                        state->snap.input[start + i] =
                            tolower(state->snap.input[start + i]);
                    }
                    if (state->snap.cursor > state->visual_start) {
                        state->snap.cursor -= size - 1;
                    }
                    state->mode = MODE_NORMAL;
                    push_history(state);
                }; break;
                case 'U': {
                    for (uint32_t i = 0; i < size; ++i) {
                        state->snap.input[start + i] =
                            toupper(state->snap.input[start + i]);
                    }
                    if (state->snap.cursor > state->visual_start) {
                        state->snap.cursor -= size - 1;
                    }
                    state->mode = MODE_NORMAL;
                    push_history(state);
                }; break;
                default:
                    break;
            }
        } break;
    }
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

    State state = {
        .mode = MODE_NORMAL,
        .snap =
            {
                .input = "abc def ghi jkl lmn opq rst uvw xyz",
                .input_len = 9 * 3 + 8,
                .cursor = 0,
                .offset = 0,
            },
        .visual_start = 0,
        .history =
            {
                .snaps = {{{0}}},
                .len = 0,
                .index = 0,
            },
        .filename = argc > 1 ? argv[1] : NULL,
    };

    initscr();
    noecho();              // Disable echoing
    cbreak();              // Disable line buffering
    keypad(stdscr, TRUE);  // Enable raw key input
    set_escdelay(0);       // Disable Escape key delay

    signal(SIGINT, terminate);  // Clean up on SIGINT

    start_color();         // Enable color
    use_default_colors();  // Don't change the background color

    init_pair(PAIR_BOX, COLOR_BLUE, -1);
    init_pair(PAIR_DETAILS, COLOR_WHITE, -1);
    init_pair(PAIR_VISUAL, -1, COLOR_BLUE);

    push_history(&state);

    int key = 0;
    while (TRUE) {
        frame(&state, &key);
    }

    endwin();
    return 0;
}
