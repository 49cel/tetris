/*
 * Terminal Tetris — Data Structures Project
 *
 * Data structures (all in data_structures.h):
 *   Matrix      2D array for the game board
 *   Queue<int>  linked-list FIFO for upcoming pieces (7-bag randomiser)
 *   Stack       array-backed LIFO for game-state history / statistics
 *   LinkedList  sorted singly linked list for the high-score leaderboard
 *   hash maps   std::unordered_map isn't used explicitly, but the shape
 *               and colour tables serve the same role (O(1) lookup by index)
 *
 * Build:  make          (or  g++ -std=c++17 -O2 -o tetris main.cpp -lncursesw)
 * Run:    ./tetris
 */

#include <ncurses.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "data_structures.h"

// ── Constants ────────────────────────────────────────────────────

static constexpr int BOARD_ROWS = 20;
static constexpr int BOARD_COLS = 10;
static constexpr int CELL_W     = 2;
static constexpr int NUM_PIECES = 7;

static const int LINE_PTS[] = {0, 100, 300, 500, 800};

// ── Shape Definitions ────────────────────────────────────────────

struct ShapeDef { int grid[4][4]; int dim; };

static const ShapeDef SHAPES[NUM_PIECES] = {
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}, 4},   // I
    {{{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, 2},   // O
    {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, 3},   // T
    {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, 3},   // S
    {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}, 3},   // Z
    {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, 3},   // J
    {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, 3},   // L
};


// ── Difficulty ───────────────────────────────────────────────────

struct DiffInfo { const char* name; double interval; double accel; const char* desc; };

static const DiffInfo DIFFS[] = {
    {"Easy",   0.80, 0.05, "Relaxed pace - great for beginners."},
    {"Medium", 0.50, 0.04, "Standard Tetris speed."},
    {"Hard",   0.30, 0.03, "Fast drops - for experienced players."},
};
static constexpr int NUM_DIFFS = 3;

// ── Title Letters (each coloured differently) ────────────────────

struct TitleLetter { const char* rows[5]; int color; int width; };

static const TitleLetter TITLE[] = {
    {{"██████","  ██  ","  ██  ","  ██  ","  ██  "}, 1, 6},
    {{"██████","██    ","████  ","██    ","██████"}, 2, 6},
    {{"██████","  ██  ","  ██  ","  ██  ","  ██  "}, 3, 6},
    {{"█████ ","██  ██","█████ ","██ ██ ","██  ██"}, 4, 6},
    {{"██","██","██","██","██"},                      5, 2},
    {{"██████","██    ","██████","    ██","██████"}, 6, 6},
};
static constexpr int TITLE_COUNT = 6;

// ── Scores File Path ─────────────────────────────────────────────

static std::string gScoresPath;

static std::string exeDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string p(buf);
        auto pos = p.rfind('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return ".";
}

// ── Utility ──────────────────────────────────────────────────────

static void put(int y, int x, const std::string& s, int attr = A_NORMAL) {
    int h, w;
    getmaxyx(stdscr, h, w);
    if (y < 0 || y >= h || x < 0 || x >= w || s.empty()) return;
    attrset(attr);
    mvaddstr(y, x, s.c_str());
    attrset(A_NORMAL);
}

static std::string border_hline(int cells) {
    std::string s;
    for (int i = 0; i < cells; ++i) s += "──";
    return s;
}

// ── Colour Setup ─────────────────────────────────────────────────

// Pairs  1-7  : piece foreground on black  (text / ghost / title)
// Pairs 11-17 : black on piece background  (filled blocks)
// Pair  20    : white on black   (border / general text)
// Pair  21    : cyan  on black   (headings)
// Pair  22    : black on cyan    (menu highlight)

static void setupColors() {
    start_color();
    use_default_colors();
    if (can_change_color())
        init_color(COLOR_BLACK, 0, 0, 0);

    static const short cols[] = {
        COLOR_CYAN, COLOR_YELLOW, COLOR_MAGENTA,
        COLOR_GREEN, COLOR_RED, COLOR_BLUE, COLOR_WHITE,
    };
    for (int i = 0; i < 7; ++i) {
        init_pair(i + 1,  cols[i],      COLOR_BLACK);
        init_pair(i + 11, COLOR_BLACK,  cols[i]);
    }
    init_pair(20, COLOR_WHITE, COLOR_BLACK);
    init_pair(21, COLOR_CYAN,  COLOR_BLACK);
    init_pair(22, COLOR_BLACK, COLOR_CYAN);
}

// ── Piece ────────────────────────────────────────────────────────

struct Piece {
    int shape[4][4]{};
    int dim   = 0;
    int type  = 0;
    int color = 0;
    int row   = 0;
    int col   = 0;

    void init(int t) {
        type  = t;
        dim   = SHAPES[t].dim;
        color = t + 1;
        row   = 0;
        col   = BOARD_COLS / 2 - dim / 2;
        std::memcpy(shape, SHAPES[t].grid, sizeof(shape));
    }

    void rotateCW() {
        int tmp[4][4]{};
        for (int r = 0; r < dim; ++r)
            for (int c = 0; c < dim; ++c)
                tmp[r][c] = shape[dim - 1 - c][r];
        std::memcpy(shape, tmp, sizeof(shape));
    }

    using Cells = std::vector<std::pair<int,int>>;

    Cells cells() const {
        Cells out;
        for (int r = 0; r < dim; ++r)
            for (int c = 0; c < dim; ++c)
                if (shape[r][c]) out.emplace_back(row + r, col + c);
        return out;
    }
};

// ── Game State Snapshot ──────────────────────────────────────────

struct GameState { int score; int level; int lines; };

// ── Tetris Game Engine ───────────────────────────────────────────

class TetrisGame {
    Queue<int>       pieceQueue_;
    Stack<GameState> history_;
    std::mt19937     rng_{std::random_device{}()};
    double           accel_;

    void fillQueue() {
        int bag[NUM_PIECES];
        for (int i = 0; i < NUM_PIECES; ++i) bag[i] = i;
        std::shuffle(bag, bag + NUM_PIECES, rng_);
        for (int i = 0; i < NUM_PIECES; ++i) pieceQueue_.enqueue(bag[i]);
    }

    bool valid(const Piece::Cells& cells) const {
        for (auto [r, c] : cells) {
            if (c < 0 || c >= BOARD_COLS || r >= BOARD_ROWS) return false;
            if (r >= 0 && board.get(r, c) != 0) return false;
        }
        return true;
    }

    void spawn() {
        if (pieceQueue_.size() < NUM_PIECES) fillQueue();
        piece.init(pieceQueue_.dequeue());
        hasPiece = true;
        if (!valid(piece.cells())) { gameOver = true; }
    }

    void lock() {
        for (auto [r, c] : piece.cells())
            if (r >= 0) board.set(r, c, piece.color);
        clearLines();
        history_.push({score, level, linesCleared});
        spawn();
    }

    void clearLines() {
        int cleared = 0;
        for (int r = 0; r < BOARD_ROWS; ++r)
            if (board.isRowFull(r)) { board.clearRow(r); ++cleared; }
        if (cleared) {
            linesCleared += cleared;
            score += LINE_PTS[std::min(cleared, 4)] * level;
            int nl = linesCleared / 10 + 1;
            if (nl > level) {
                level = nl;
                dropInterval = std::max(0.05,
                    DIFFS[difficulty].interval - (level - 1) * accel_);
            }
        }
    }

public:
    Matrix board{BOARD_ROWS, BOARD_COLS};
    Piece  piece;
    int    score        = 0;
    int    level        = 1;
    int    linesCleared = 0;
    int    difficulty   = 1;
    double dropInterval = 0.5;
    bool   gameOver     = false;
    bool   hasPiece     = false;

    TetrisGame(const TetrisGame&) = delete;
    TetrisGame& operator=(const TetrisGame&) = delete;

    explicit TetrisGame(int diff) : difficulty(diff) {
        dropInterval = DIFFS[diff].interval;
        accel_       = DIFFS[diff].accel;
        fillQueue();
        spawn();
    }

    void moveLeft() {
        Piece::Cells moved;
        for (auto [r, c] : piece.cells()) moved.emplace_back(r, c - 1);
        if (valid(moved)) --piece.col;
    }

    void moveRight() {
        Piece::Cells moved;
        for (auto [r, c] : piece.cells()) moved.emplace_back(r, c + 1);
        if (valid(moved)) ++piece.col;
    }

    bool moveDown() {
        Piece::Cells moved;
        for (auto [r, c] : piece.cells()) moved.emplace_back(r + 1, c);
        if (valid(moved)) { ++piece.row; return true; }
        lock();
        return false;
    }

    void rotate() {
        int saved[4][4];
        std::memcpy(saved, piece.shape, sizeof(saved));
        piece.rotateCW();
        if (valid(piece.cells())) return;
        for (int off : {1, -1, 2, -2}) {
            piece.col += off;
            if (valid(piece.cells())) return;
            piece.col -= off;
        }
        std::memcpy(piece.shape, saved, sizeof(saved));
    }

    void hardDrop() {
        while (moveDown()) score += 2;
    }

    int ghostRow() const {
        int gr = piece.row;
        for (;;) {
            Piece::Cells test;
            for (int r = 0; r < piece.dim; ++r)
                for (int c = 0; c < piece.dim; ++c)
                    if (piece.shape[r][c])
                        test.emplace_back(gr + r + 1, piece.col + c);
            if (valid(test)) ++gr; else break;
        }
        return gr;
    }

    int nextType() const {
        return pieceQueue_.isEmpty() ? -1 : pieceQueue_.peek();
    }

    int historySize() const { return history_.size(); }
};

// ── High Score Manager ───────────────────────────────────────────

class HighScoreManager {
    LinkedList list_;

public:
    HighScoreManager() { load(); }

    void load() {
        std::ifstream f(gScoresPath);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string name, diff;
            int sc, lv;
            char d;
            if (std::getline(ss, name, '|') && ss >> sc && ss.get(d) &&
                std::getline(ss, diff, '|') && ss >> lv)
                list_.insertSorted({name, sc, diff, lv});
        }
    }

    void save() {
        std::ofstream f(gScoresPath);
        auto top = list_.toVector();
        int n = 0;
        for (auto& e : top) {
            if (++n > 20) break;
            f << e.name << '|' << e.score << '|'
              << e.difficulty << '|' << e.level << '\n';
        }
    }

    void add(const std::string& name, int sc, int diff, int lv) {
        list_.insertSorted({name, sc, DIFFS[diff].name, lv});
        save();
    }

    int highest() const { return list_.getHighest(); }

    std::vector<ScoreEntry> top(int n = 10) { return list_.getTop(n); }
};

// ── Drawing Helpers ──────────────────────────────────────────────

static void drawTitle(int y, int cx) {
    int tw = 0;
    for (int i = 0; i < TITLE_COUNT; ++i) tw += TITLE[i].width;
    tw += TITLE_COUNT - 1;
    int sx = cx - tw / 2;

    for (int ri = 0; ri < 5; ++ri) {
        int x = sx;
        for (int li = 0; li < TITLE_COUNT; ++li) {
            const char* row = TITLE[li].rows[ri];
            int ci = TITLE[li].color;
            int len = (int)strlen(row);
            const unsigned char* p = (const unsigned char*)row;
            for (int bi = 0; bi < len; ) {
                int bytes = 1;
                if (p[bi] >= 0xF0)      bytes = 4;
                else if (p[bi] >= 0xE0) bytes = 3;
                else if (p[bi] >= 0xC0) bytes = 2;
                std::string ch((const char*)p + bi, bytes);
                if (ch != " ")
                    put(y + ri, x, ch, COLOR_PAIR(ci) | A_BOLD);
                ++x;
                bi += bytes;
            }
            ++x;
        }
    }
}

static void drawBoard(const TetrisGame& g, int oy, int ox) {
    int dim = COLOR_PAIR(20) | A_DIM;
    put(oy, ox, "┌" + border_hline(BOARD_COLS) + "┐", dim);
    for (int r = 0; r < BOARD_ROWS; ++r) {
        put(oy + 1 + r, ox, "│", dim);
        for (int c = 0; c < BOARD_COLS; ++c) {
            int v = g.board.get(r, c);
            int px = ox + 1 + c * CELL_W;
            int py = oy + 1 + r;
            if (v) put(py, px, "  ", COLOR_PAIR(v + 10));
            else   put(py, px, "  ");
        }
        put(oy + 1 + r, ox + 1 + BOARD_COLS * CELL_W, "│", dim);
    }
    put(oy + 1 + BOARD_ROWS, ox, "└" + border_hline(BOARD_COLS) + "┘", dim);

    if (!g.hasPiece) return;
    const Piece& pc = g.piece;

    int gr = g.ghostRow();
    if (gr != pc.row) {
        int ga = COLOR_PAIR(pc.color) | A_DIM;
        for (int r = 0; r < pc.dim; ++r)
            for (int c = 0; c < pc.dim; ++c)
                if (pc.shape[r][c]) {
                    int br = gr + r, bc = pc.col + c;
                    if (br >= 0 && br < BOARD_ROWS && bc >= 0 && bc < BOARD_COLS
                        && g.board.get(br, bc) == 0)
                        put(oy + 1 + br, ox + 1 + bc * CELL_W, "░░", ga);
                }
    }

    int pa = COLOR_PAIR(pc.color + 10);
    for (auto [r, c] : pc.cells())
        if (r >= 0 && r < BOARD_ROWS && c >= 0 && c < BOARD_COLS)
            put(oy + 1 + r, ox + 1 + c * CELL_W, "  ", pa);
}

static void drawPanel(const TetrisGame& g, int hi, int oy, int ox) {
    put(oy, ox, "T E T R I S", COLOR_PAIR(21) | A_BOLD);
    int y = oy + 2;
    put(y,     ox, "Score: " + std::to_string(g.score),        COLOR_PAIR(2) | A_BOLD);
    put(y + 1, ox, "High:  " + std::to_string(hi),             COLOR_PAIR(4) | A_BOLD);
    put(y + 2, ox, "Level: " + std::to_string(g.level),        COLOR_PAIR(20));
    put(y + 3, ox, "Lines: " + std::to_string(g.linesCleared), COLOR_PAIR(20));
    put(y + 4, ox, std::string("Mode:  ") + DIFFS[g.difficulty].name, COLOR_PAIR(20));
    y += 6;

    put(y, ox, "Next:", COLOR_PAIR(20));
    ++y;
    int nt = g.nextType();
    if (nt >= 0) {
        const auto& sh = SHAPES[nt];
        int ci  = nt + 1;
        int sw  = sh.dim * CELL_W;
        int da  = COLOR_PAIR(20) | A_DIM;
        put(y, ox, "┌" + border_hline(sh.dim) + "┐", da);
        for (int r = 0; r < sh.dim; ++r) {
            put(y + 1 + r, ox, "│", da);
            for (int c = 0; c < sh.dim; ++c) {
                int px = ox + 1 + c * CELL_W;
                if (sh.grid[r][c]) put(y + 1 + r, px, "  ", COLOR_PAIR(ci + 10));
                else               put(y + 1 + r, px, "  ");
            }
            put(y + 1 + r, ox + 1 + sw, "│", da);
        }
        put(y + 1 + sh.dim, ox, "└" + border_hline(sh.dim) + "┘", da);
        y += sh.dim + 3;
    }
    y += 1;
    int da = COLOR_PAIR(20) | A_DIM;
    put(y, ox, "Controls:", da); ++y;
    put(y, ox, " ← →   Move",      da); ++y;
    put(y, ox, " ↑     Rotate",    da); ++y;
    put(y, ox, " ↓     Soft drop", da); ++y;
    put(y, ox, " Space Hard drop",da); ++y;
    put(y, ox, " P     Pause",    da); ++y;
    put(y, ox, " Q     Quit",     da);
}

static void drawGame(const TetrisGame& g, int hi, bool paused = false) {
    erase();
    int h, w;
    getmaxyx(stdscr, h, w);
    int bw = BOARD_COLS * CELL_W + 2;
    int tw = bw + 22;
    int oy = std::max(0, (h - (BOARD_ROWS + 2)) / 2);
    int ox = std::max(0, (w - tw) / 2);
    drawBoard(g, oy, ox);
    drawPanel(g, hi, oy, ox + bw + 2);
    if (paused) {
        int py = oy + BOARD_ROWS / 2;
        int px = ox + (bw - 10) / 2;
        put(py, px, "  PAUSED  ", COLOR_PAIR(22) | A_BOLD);
    }
    refresh();
}

// ── Name Entry ───────────────────────────────────────────────────

static std::string getName(int finalScore) {
    curs_set(1);
    std::string name;
    timeout(-1);
    for (;;) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        int cx = w / 2;
        put(h/2 - 4, cx - 5, "GAME OVER!", COLOR_PAIR(5) | A_BOLD);
        put(h/2 - 2, cx - 9, "Final Score: " + std::to_string(finalScore),
            COLOR_PAIR(2) | A_BOLD);
        put(h/2,     cx - 9, "Enter your name:", COLOR_PAIR(20));
        put(h/2 + 2, cx - 5, name + "█", COLOR_PAIR(21));
        put(h/2 + 4, cx - 15, "(Max 10 chars, Enter to confirm)",
            COLOR_PAIR(20) | A_DIM);
        refresh();

        int key = getch();
        if (key == '\n' || key == '\r' || key == KEY_ENTER) break;
        else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (!name.empty()) name.pop_back();
        } else if (key >= 32 && key <= 126 && (int)name.size() < 10) {
            name += (char)key;
        }
    }
    curs_set(0);
    return name.empty() ? "Player" : name;
}

// ── Scores Screen ────────────────────────────────────────────────

static void scoresScreen(HighScoreManager& sm) {
    timeout(-1);
    for (;;) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        int cx = w / 2;
        put(2, cx - 6, "HIGH SCORES", COLOR_PAIR(21) | A_BOLD);
        auto top = sm.top(10);
        if (top.empty()) {
            put(6, cx - 14, "No scores yet. Go play a game!", COLOR_PAIR(20));
        } else {
            char hdr[80];
            snprintf(hdr, sizeof(hdr), "%3s  %-10s  %8s  %3s  %-6s",
                     "#", "Name", "Score", "Lvl", "Diff");
            int hx = cx - (int)strlen(hdr) / 2;
            put(5, hx, hdr, COLOR_PAIR(20) | A_BOLD);
            std::string sep(strlen(hdr), '-');
            put(6, hx, sep, COLOR_PAIR(20) | A_DIM);
            for (int i = 0; i < (int)top.size(); ++i) {
                char line[80];
                snprintf(line, sizeof(line), "%3d  %-10s  %8d  %3d  %-6s",
                         i + 1, top[i].name.c_str(), top[i].score,
                         top[i].level, top[i].difficulty.c_str());
                int attr = i == 0 ? (COLOR_PAIR(2) | A_BOLD) : COLOR_PAIR(20);
                put(7 + i, hx, line, attr);
            }
        }
        put(h - 2, cx - 12, "Press any key to return", COLOR_PAIR(20) | A_DIM);
        refresh();
        int key = getch();
        if (key != KEY_RESIZE) return;
    }
}

// ── Data Structures Info Screen ──────────────────────────────────

static void dsInfoScreen() {
    timeout(-1);
    for (;;) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        int cx = w / 2;
        int y = 2;
        put(y, cx - 9, "DATA STRUCTURES USED", COLOR_PAIR(21) | A_BOLD);
        y += 2;

        int da = COLOR_PAIR(20);
        int hd = COLOR_PAIR(3) | A_BOLD;
        int dm = COLOR_PAIR(20) | A_DIM;

        put(y, cx - 22, "Matrix (2D Array)", hd); ++y;
        put(y, cx - 22, "  The game board. Each cell stores 0 (empty) or a", da); ++y;
        put(y, cx - 22, "  colour index. clearRow shifts row pointers for", da); ++y;
        put(y, cx - 22, "  O(row) gravity instead of O(row*cols) copying.", da); y += 2;

        put(y, cx - 22, "Queue (Linked-List FIFO)", hd); ++y;
        put(y, cx - 22, "  Manages the 7-bag piece randomiser. Pieces are", da); ++y;
        put(y, cx - 22, "  enqueued in shuffled bags and dequeued one at a", da); ++y;
        put(y, cx - 22, "  time. O(1) enqueue and dequeue.", da); y += 2;

        put(y, cx - 22, "Stack (Dynamic Array LIFO)", hd); ++y;
        put(y, cx - 22, "  Records a snapshot (score, level, lines) after", da); ++y;
        put(y, cx - 22, "  every piece lock. Amortised O(1) push with", da); ++y;
        put(y, cx - 22, "  automatic doubling when capacity is reached.", da); y += 2;

        put(y, cx - 22, "Linked List (Sorted Singly Linked)", hd); ++y;
        put(y, cx - 22, "  High-score leaderboard. insertSorted keeps", da); ++y;
        put(y, cx - 22, "  entries ranked descending so getTop(n) is O(n)", da); ++y;
        put(y, cx - 22, "  with no extra sort step.", da); y += 2;

        put(y, cx - 22, "Hash Map (Index Tables)", hd); ++y;
        put(y, cx - 22, "  Shape definitions and colour mappings — O(1)", da); ++y;
        put(y, cx - 22, "  lookup by piece-type index.", da);

        put(h - 2, cx - 12, "Press any key to return", dm);
        refresh();
        int key = getch();
        if (key != KEY_RESIZE) return;
    }
}

// ── Settings Screen ──────────────────────────────────────────────

static int settingsScreen(int cur) {
    int sel = cur;
    timeout(-1);
    for (;;) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        int cx = w / 2;
        put(2, cx - 4, "SETTINGS", COLOR_PAIR(21) | A_BOLD);
        put(4, cx - 6, "Difficulty:", COLOR_PAIR(20));
        for (int i = 0; i < NUM_DIFFS; ++i) {
            std::string text = std::string("  ") + DIFFS[i].name + "  ";
            int attr = (i == sel) ? (COLOR_PAIR(22) | A_BOLD) : COLOR_PAIR(20);
            put(6 + i * 2, cx - (int)text.size() / 2, text, attr);
        }
        int dy = 6 + NUM_DIFFS * 2 + 1;
        std::string desc = DIFFS[sel].desc;
        put(dy, cx - (int)desc.size() / 2, desc, COLOR_PAIR(20) | A_DIM);
        put(h - 2, cx - 17, "↑↓ Select   Enter Confirm   Esc Back",
            COLOR_PAIR(20) | A_DIM);
        refresh();

        int key = getch();
        if (key == KEY_UP)   sel = (sel + NUM_DIFFS - 1) % NUM_DIFFS;
        else if (key == KEY_DOWN) sel = (sel + 1) % NUM_DIFFS;
        else if (key == '\n' || key == '\r' || key == KEY_ENTER) return sel;
        else if (key == 27 || key == 'q' || key == 'Q') return cur;
    }
}

// ── Game Loop ────────────────────────────────────────────────────

static void gameLoop(int diff, HighScoreManager& sm) {
    TetrisGame game(diff);
    int hi = sm.highest();
    timeout(50);
    auto lastDrop = std::chrono::steady_clock::now();
    bool paused = false;

    while (!game.gameOver) {
        int key = getch();

        if (key == KEY_RESIZE) { clear(); drawGame(game, hi, paused); continue; }
        if (key == 'q' || key == 'Q') { timeout(-1); return; }
        if (key == 'p' || key == 'P') {
            paused = !paused;
            drawGame(game, hi, paused);
            continue;
        }

        if (!paused) {
            if      (key == KEY_LEFT)  game.moveLeft();
            else if (key == KEY_RIGHT) game.moveRight();
            else if (key == KEY_DOWN) {
                if (game.moveDown()) game.score += 1;
                lastDrop = std::chrono::steady_clock::now();
            }
            else if (key == KEY_UP)    game.rotate();
            else if (key == ' ')     { game.hardDrop(); lastDrop = std::chrono::steady_clock::now(); }

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - lastDrop).count();
            if (elapsed >= game.dropInterval) {
                game.moveDown();
                lastDrop = now;
            }
            if (game.score > hi) hi = game.score;
        }

        drawGame(game, hi, paused);
    }

    // Game over overlay
    timeout(-1);
    drawGame(game, hi);
    int h, w;
    getmaxyx(stdscr, h, w);
    int bw = BOARD_COLS * CELL_W + 2;
    int tw = bw + 22;
    int oy = std::max(0, (h - (BOARD_ROWS + 2)) / 2);
    int ox = std::max(0, (w - tw) / 2);
    put(oy + BOARD_ROWS / 2, ox + (bw - 12) / 2, " GAME  OVER ",
        COLOR_PAIR(5) | A_BOLD | A_REVERSE);
    refresh();
    napms(1800);
    flushinp();

    std::string name = getName(game.score);
    sm.add(name, game.score, diff, game.level);
}

// ── Main Menu ────────────────────────────────────────────────────

static void mainMenu() {
    int diff = 1;
    HighScoreManager sm;
    const char* opts[] = {"Play", "Settings", "High Scores", "Data Structures", "Quit"};
    constexpr int NOPTS = 5;
    int sel = 0;
    timeout(-1);

    for (;;) {
        erase();
        int h, w;
        getmaxyx(stdscr, h, w);
        int cx = w / 2;
        drawTitle(2, cx);
        int my = 9;
        for (int i = 0; i < NOPTS; ++i) {
            std::string text = std::string("  ") + opts[i] + "  ";
            int attr = (i == sel) ? (COLOR_PAIR(22) | A_BOLD) : COLOR_PAIR(20);
            put(my + i * 2, cx - (int)text.size() / 2, text, attr);
        }
        put(my + NOPTS * 2 + 1, cx - 12,
            std::string("Difficulty: ") + DIFFS[diff].name,
            COLOR_PAIR(20) | A_DIM);
        put(h - 2, cx - 14, "↑↓ Navigate   Enter Select",
            COLOR_PAIR(20) | A_DIM);
        refresh();

        int key = getch();
        if (key == KEY_UP)        sel = (sel + NOPTS - 1) % NOPTS;
        else if (key == KEY_DOWN) sel = (sel + 1) % NOPTS;
        else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            if (sel == 0)      gameLoop(diff, sm);
            else if (sel == 1) diff = settingsScreen(diff);
            else if (sel == 2) scoresScreen(sm);
            else if (sel == 3) dsInfoScreen();
            else               return;
        }
        else if (key == 'q' || key == 'Q') return;
    }
}

// ── Entry Point ──────────────────────────────────────────────────

int main() {
    std::setlocale(LC_ALL, "");
    gScoresPath = exeDir() + "/highscores.dat";

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) setupColors();
    bkgd(COLOR_PAIR(20));

    int h, w;
    getmaxyx(stdscr, h, w);
    int minW = BOARD_COLS * CELL_W + 30, minH = BOARD_ROWS + 4;
    if (h < minH || w < minW) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Terminal too small (%dx%d). Need at least %dx%d.", w, h, minW, minH);
        mvaddstr(0, 0, msg);
        getch();
        endwin();
        return 1;
    }

    mainMenu();
    endwin();
    return 0;
}
