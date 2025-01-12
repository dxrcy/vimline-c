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

uint32_t INPUT_WIDTH = 20;
uint32_t BOX_Y = 0;
uint32_t BOX_X = 0;

enum VimMode MODE = NORMAL;
uint32_t VISUAL_START = 0;

State STATE = {
    .input = "abc def ghi jkl lmn opq rst uvw xyz",
    .input_len = 9 * 3 + 8,
    .cursor = 0,
    .offset = 0,
};

History HISTORY = {
    .states = {{{0}}},
    .len = 0,
    .index = 0,
};

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

int find_word_start(bool full_word) {
    // Empty line
    if (STATE.input_len < 1) {
        return 0;
    }
    // At end of line
    if (STATE.cursor + 1 >= STATE.input_len) {
        return STATE.input_len - 1;
    }
    // On a space
    // Look for first non-space character
    if (isspace(STATE.input[STATE.cursor])) {
        while (STATE.cursor + 1 < STATE.input_len) {
            ++STATE.cursor;
            if (!isspace(STATE.input[STATE.cursor])) {
                return STATE.cursor;
            }
        }
    }
    // On non-space
    int alnum = isalnum(STATE.input[STATE.cursor]);
    while (STATE.cursor < STATE.input_len - 1) {
        ++STATE.cursor;
        // Space found
        // Look for first non-space character
        if (isspace(STATE.input[STATE.cursor])) {
            while (STATE.cursor + 1 < STATE.input_len) {
                ++STATE.cursor;
                if (!isspace(STATE.input[STATE.cursor])) {
                    return STATE.cursor;
                }
            }
            break;
        }
        // First punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (!full_word && isalnum(STATE.input[STATE.cursor]) != alnum) {
            return STATE.cursor;
        }
    }
    // No next word found
    // Go to end of line
    return STATE.input_len - 1;
}

int find_word_end(bool full_word) {
    // Empty line
    if (STATE.input_len < 1) {
        return 0;
    }
    // At end of line
    if (STATE.cursor + 1 >= STATE.input_len) {
        return STATE.input_len - 1;
    }
    ++STATE.cursor;  // Always move at least one character
    // On a sequence of spaces (>=1)
    // Look for start of next word, start from there instead
    while (STATE.cursor + 1 < STATE.input_len &&
           isspace(STATE.input[STATE.cursor])) {
        ++STATE.cursor;
    }
    // On non-space
    int alnum = isalnum(STATE.input[STATE.cursor]);
    while (STATE.cursor < STATE.input_len) {
        ++STATE.cursor;
        // Space found
        // Word ends at previous index
        // OR first punctuation after word
        // OR first word after punctuation
        // (If distinguishing words and punctuation)
        if (isspace(STATE.input[STATE.cursor]) ||
            (!full_word && isalnum(STATE.input[STATE.cursor]) != alnum)) {
            return STATE.cursor - 1;
        }
    }
    // No next word found
    // Go to end of line
    return STATE.input_len - 1;
}

int find_word_back(bool full_word) {
    // At start of line
    if (STATE.cursor <= 1) {
        return 0;
    }
    // Start at previous character
    --STATE.cursor;
    // On a sequence of spaces (>=1)
    // Look for end of previous word, start from there instead
    while (STATE.cursor > 0 && isspace(STATE.input[STATE.cursor])) {
        --STATE.cursor;
    }
    // Now on a non-space
    int alnum = isalnum(STATE.input[STATE.cursor]);
    while (STATE.cursor > 0) {
        STATE.cursor--;
        // Space found
        // OR first punctuation before word
        // OR first word before punctuation
        // Word starts at next index
        // (If distinguishing words and punctuation)
        if (isspace(STATE.input[STATE.cursor]) ||
            (!full_word && isalnum(STATE.input[STATE.cursor]) != alnum)) {
            return STATE.cursor + 1;
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

void push_history() {
    // Delete all future history to be overwritten
    if (HISTORY.index <= HISTORY.len) {
        HISTORY.len = HISTORY.index;
    }
    // Ignore if same as last entry
    if (HISTORY.len > 0 &&
        equals_state_input(&STATE, &HISTORY.states[HISTORY.len - 1])) {
        return;
    }
    if (HISTORY.len >= MAX_HISTORY) {
        for (uint32_t i = 1; i < HISTORY.len; ++i) {
            copy_state(&HISTORY.states[i], &HISTORY.states[i - 1]);
        }
    } else {
        ++HISTORY.len;
        ++HISTORY.index;
    }

    copy_state(&STATE, &HISTORY.states[HISTORY.index - 1]);
}

void undo_history() {
    if (HISTORY.len == 0 || HISTORY.index <= 0) {
        return;
    }
    --HISTORY.index;
    copy_state(&HISTORY.states[HISTORY.index], &STATE);
}
void redo_history() {
    if (HISTORY.index + 1 >= HISTORY.len) {
        return;
    }
    ++HISTORY.index;
    copy_state(&HISTORY.states[HISTORY.index], &STATE);
}

void save_input(const char* const filename) {
    // If no output file is specified, print instead
    if (filename == NULL) {
        for (uint32_t i = 0; i < STATE.input_len; ++i) {
            printf("%c", STATE.input[i]);
        }
        printf("\n");
        return;
    }

    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        perror("Failed to open file");
        exit(1);
    }

    for (uint32_t i = 0; i < STATE.input_len; ++i) {
        if (fprintf(file, "%c", STATE.input[i]) < 1) {
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

void update_offset_left() {
    if (STATE.cursor < STATE.offset + CURSOR_LEFT) {
        STATE.offset = subsat(STATE.cursor, CURSOR_LEFT);
    }
}
void update_offset_right() {
    if (STATE.cursor + CURSOR_RIGHT > INPUT_WIDTH) {
        STATE.offset = subsat(STATE.cursor + CURSOR_RIGHT, INPUT_WIDTH);
    }
}

bool in_visual_select(uint32_t index) {
    if (STATE.cursor == VISUAL_START) {
        return index == VISUAL_START;
    }
    if (STATE.cursor < VISUAL_START) {
        return index >= STATE.cursor && index <= VISUAL_START;
    }
    return index >= VISUAL_START && index <= STATE.cursor;
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

        INPUT_WIDTH = min(max_cols - BOX_MARGIN * 2 - 2, MAX_INPUT_WIDTH);
        BOX_X = (max_cols - INPUT_WIDTH) / 2 - 1;
        BOX_Y = max_rows / 2 - 1;

        attron(attr_box);
        draw_box_outline(
            BOX_X,
            BOX_Y,
            INPUT_WIDTH + 2,
            STATE.offset > 0,
            STATE.offset + INPUT_WIDTH < STATE.input_len
        );
        attroff(attr_box);

        move(BOX_Y + 1, BOX_X + 1);
        for (uint32_t i = 0; i < INPUT_WIDTH; ++i) {
            uint32_t index = i + STATE.offset;
            if (index < STATE.input_len) {
                if (MODE == VISUAL && in_visual_select(index)) {
                    attron(attr_visual);
                }
                printw("%c", STATE.input[index]);
                attroff(attr_visual);
            } else {
                printw(" ");
            }
        }

        move(max_rows - 1, 0);
        attron(attr_details);
        printw("%8s", mode_name(MODE));
        printw(" [%3d /%3d]", STATE.cursor, STATE.input_len);
        printw(" [%3d /%3d]", HISTORY.index, HISTORY.len);
        printw(" 0x%02x", key);
        attroff(attr_details);

        set_cursor(MODE);
        move(BOX_Y + 1, BOX_X + subsat(STATE.cursor, STATE.offset) + 1);

        refresh();

        key = getch();

        switch (MODE) {
            case NORMAL:
                switch (key) {
                    case 'q':
                        /* case K_ESCAPE: */
                        endwin();
                        exit(0);
                        break;
                    case K_RETURN:
                        endwin();
                        save_input(filename);
                        exit(0);
                        break;
                    case 'r':
                        MODE = REPLACE;
                        break;
                    case 'v':
                        MODE = VISUAL;
                        VISUAL_START = STATE.cursor;
                        break;
                    case 'V':
                        MODE = VISUAL;
                        VISUAL_START = 0;
                        STATE.cursor = STATE.input_len - 1;
                        break;
                    case 'i':
                        MODE = INSERT;
                        break;
                    case 'a':
                        MODE = INSERT;
                        if (STATE.cursor < STATE.input_len) {
                            ++STATE.cursor;
                        }
                        break;
                    case 'I':
                        MODE = INSERT;
                        STATE.cursor = 0;
                        STATE.offset = 0;
                        break;
                    case 'A':
                        MODE = INSERT;
                        STATE.cursor = STATE.input_len;
                        STATE.offset = subsat(STATE.cursor + 1, INPUT_WIDTH);
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (STATE.cursor > 0) {
                            --STATE.cursor;
                            update_offset_left();
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (STATE.cursor < MAX_INPUT - 1 &&
                            STATE.cursor < STATE.input_len - 1) {
                            ++STATE.cursor;
                            update_offset_right();
                        }
                        break;
                    case 'w':
                        STATE.cursor = find_word_start(FALSE);
                        update_offset_right();
                        break;
                    case 'e':
                        STATE.cursor = find_word_end(FALSE);
                        update_offset_right();
                        break;
                    case 'b':
                        STATE.cursor = find_word_back(FALSE);
                        update_offset_left();
                        break;
                    case 'W':
                        STATE.cursor = find_word_start(TRUE);
                        update_offset_right();
                        break;
                    case 'E':
                        STATE.cursor = find_word_end(TRUE);
                        update_offset_right();
                        break;
                    case 'B':
                        STATE.cursor = find_word_back(TRUE);
                        update_offset_left();
                        break;
                    case '^':
                    case '_':
                        for (STATE.cursor = 0; STATE.cursor < STATE.input_len;
                             ++STATE.cursor) {
                            if (!isspace(STATE.input[STATE.cursor])) {
                                break;
                            }
                        }
                        update_offset_left();
                        break;
                    case '0':
                        STATE.cursor = 0;
                        STATE.offset = 0;
                        break;
                    case '$':
                        STATE.cursor = STATE.input_len - 1;
                        STATE.offset = subsat(STATE.cursor + 2, INPUT_WIDTH);
                        break;
                    case 'D':
                        STATE.input_len = STATE.cursor;
                        push_history();
                        break;
                    case 'x':
                        if (STATE.input_len > 0) {
                            for (uint32_t i = STATE.cursor + 1;
                                 i < STATE.input_len;
                                 ++i) {
                                STATE.input[i - 1] = STATE.input[i];
                            }
                            --STATE.input_len;
                            if (STATE.cursor >= STATE.input_len &&
                                STATE.input_len > 0) {
                                STATE.cursor = STATE.input_len - 1;
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
                        MODE = NORMAL;
                        if (STATE.cursor > 0) {
                            --STATE.cursor;
                        }
                        push_history();
                        break;
                    case K_RETURN:
                        endwin();
                        save_input(filename);
                        exit(0);
                        break;
                    case K_LEFT:
                        if (STATE.cursor > 0) {
                            --STATE.cursor;
                            update_offset_left();
                        }
                        break;
                    case K_RIGHT:
                        if (STATE.cursor < MAX_INPUT &&
                            STATE.cursor < STATE.input_len) {
                            ++STATE.cursor;
                            update_offset_right();
                        }
                        break;
                    case K_BACKSPACE:
                        if (STATE.cursor > 0 && STATE.input_len > 0) {
                            for (uint32_t i = STATE.cursor; i < STATE.input_len;
                                 ++i) {
                                STATE.input[i - 1] = STATE.input[i];
                            }
                            for (uint32_t i = STATE.input_len; i < MAX_INPUT;
                                 ++i) {
                                STATE.input[i] = '.';
                            }
                            --STATE.input_len;
                            --STATE.cursor;
                            update_offset_left();
                        }
                        break;
                    default:
                        if (isprint(key) && STATE.input_len < MAX_INPUT) {
                            for (uint32_t i = STATE.input_len;
                                 i >= STATE.cursor + 1;
                                 --i) {
                                STATE.input[i] = STATE.input[i - 1];
                            }
                            STATE.input[STATE.cursor] = key;
                            ++STATE.cursor;
                            ++STATE.input_len;
                            update_offset_right();
                        }
                        break;
                };
                break;

            case REPLACE:
                switch (key) {
                    case K_ESCAPE:
                        MODE = NORMAL;
                        break;
                    default:
                        if (isprint(key)) {
                            STATE.input[STATE.cursor] = key;
                            MODE = NORMAL;
                            push_history();
                        }
                        break;
                }
                break;

            case VISUAL: {
                uint32_t start = min(STATE.cursor, VISUAL_START);
                uint32_t size = difference(STATE.cursor, VISUAL_START) + 1;
                switch (key) {
                    case K_ESCAPE:
                        MODE = NORMAL;
                        break;
                    case 'h':
                    case KEY_LEFT:
                        if (STATE.cursor > 0) {
                            --STATE.cursor;
                            update_offset_left();
                        }
                        break;
                    case 'l':
                    case KEY_RIGHT:
                        if (STATE.cursor < MAX_INPUT - 1 &&
                            STATE.cursor < STATE.input_len - 1) {
                            ++STATE.cursor;
                            update_offset_right();
                        }
                        break;
                    case 'w':
                        STATE.cursor = find_word_start(FALSE);
                        update_offset_right();
                        break;
                    case 'e':
                        STATE.cursor = find_word_end(FALSE);
                        update_offset_right();
                        break;
                    case 'b':
                        STATE.cursor = find_word_back(FALSE);
                        update_offset_left();
                        break;
                    case 'W':
                        STATE.cursor = find_word_start(TRUE);
                        update_offset_right();
                        break;
                    case 'E':
                        STATE.cursor = find_word_end(TRUE);
                        update_offset_right();
                        break;
                    case 'B':
                        STATE.cursor = find_word_back(TRUE);
                        update_offset_left();
                        break;
                    case '^':
                    case '_':
                        for (STATE.cursor = 0; STATE.cursor < STATE.input_len;
                             ++STATE.cursor) {
                            if (!isspace(STATE.input[STATE.cursor])) {
                                break;
                            }
                        }
                        update_offset_left();
                        break;
                    case '0':
                        STATE.cursor = 0;
                        STATE.offset = 0;
                        break;
                    case '$':
                        STATE.cursor = STATE.input_len - 1;
                        STATE.offset = subsat(STATE.cursor + 2, INPUT_WIDTH);
                        break;
                    case 'd':
                    case 'x': {
                        for (uint32_t i = start; i <= STATE.input_len - size;
                             ++i) {
                            uint32_t new = i + size;
                            if (new >= STATE.input_len) {
                                break;
                            }
                            STATE.input[i] = STATE.input[new];
                        }
                        STATE.input_len -= size;
                        if (STATE.cursor > VISUAL_START) {
                            STATE.cursor -= size - 1;
                        }
                        if (STATE.cursor + 1 >= STATE.input_len) {
                            STATE.cursor = subsat(STATE.input_len, 1);
                        }
                        MODE = NORMAL;
                        push_history();
                    } break;
                    case 'u': {
                        for (uint32_t i = 0; i < size; ++i) {
                            STATE.input[start + i] =
                                tolower(STATE.input[start + i]);
                        }
                        if (STATE.cursor > VISUAL_START) {
                            STATE.cursor -= size - 1;
                        }
                        MODE = NORMAL;
                        push_history();
                    }; break;
                    case 'U': {
                        for (uint32_t i = 0; i < size; ++i) {
                            STATE.input[start + i] =
                                toupper(STATE.input[start + i]);
                        }
                        if (STATE.cursor > VISUAL_START) {
                            STATE.cursor -= size - 1;
                        }
                        MODE = NORMAL;
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
