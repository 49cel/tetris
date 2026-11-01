/*
 * Custom Data Structure Implementations for Tetris
 *
 * Demonstrates core data structures:
 *   Node<T>       — generic building block for linked structures
 *   LinkedList    — sorted singly linked list (high score leaderboard)
 *   Queue<T>      — FIFO queue via singly linked list (piece management)
 *   Stack<T>      — LIFO stack via dynamic array (game state history)
 *   Matrix        — 2D array with board-specific helpers (game board)
 */

#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <stdexcept>
#include <vector>
#include <string>
#include <cstring>

// ── Node ─────────────────────────────────────────────────────────
// Generic node used by Queue and LinkedList.

template <typename T>
struct Node {
    T data;
    Node* next;
    explicit Node(const T& d) : data(d), next(nullptr) {}
};

// ── Queue ────────────────────────────────────────────────────────
// FIFO queue backed by a singly linked list.
// enqueue O(1), dequeue O(1), peek O(1).
// Used for the 7-bag piece randomiser — pieces are enqueued in
// shuffled bags and dequeued one at a time as the player needs them.

template <typename T>
class Queue {
    Node<T>* head_;
    Node<T>* tail_;
    int size_;

public:
    Queue() : head_(nullptr), tail_(nullptr), size_(0) {}

    ~Queue() {
        while (head_) {
            Node<T>* tmp = head_;
            head_ = head_->next;
            delete tmp;
        }
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void enqueue(const T& item) {
        auto* node = new Node<T>(item);
        if (tail_) tail_->next = node;
        tail_ = node;
        if (!head_) head_ = node;
        ++size_;
    }

    T dequeue() {
        if (isEmpty()) throw std::runtime_error("dequeue from empty queue");
        T data = head_->data;
        Node<T>* tmp = head_;
        head_ = head_->next;
        if (!head_) tail_ = nullptr;
        delete tmp;
        --size_;
        return data;
    }

    T peek() const {
        if (isEmpty()) throw std::runtime_error("peek on empty queue");
        return head_->data;
    }

    bool isEmpty() const { return size_ == 0; }
    int size() const { return size_; }
};

// ── Stack ────────────────────────────────────────────────────────
// LIFO stack backed by a dynamic array with amortised doubling.
// push O(1) amortised, pop O(1), peek O(1).
// Used to record a snapshot after every piece lock so the game can
// track progression statistics (score, level, lines over time).

template <typename T>
class Stack {
    T* items_;
    int size_;
    int cap_;

    void grow() {
        int newCap = cap_ * 2;
        T* buf = new T[newCap];
        for (int i = 0; i < size_; ++i) buf[i] = items_[i];
        delete[] items_;
        items_ = buf;
        cap_ = newCap;
    }

public:
    Stack() : size_(0), cap_(16) { items_ = new T[cap_]; }
    ~Stack() { delete[] items_; }

    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;

    void push(const T& item) {
        if (size_ == cap_) grow();
        items_[size_++] = item;
    }

    T pop() {
        if (isEmpty()) throw std::runtime_error("pop from empty stack");
        return items_[--size_];
    }

    T peek() const {
        if (isEmpty()) throw std::runtime_error("peek on empty stack");
        return items_[size_ - 1];
    }

    bool isEmpty() const { return size_ == 0; }
    int size() const { return size_; }
};

// ── ScoreEntry ───────────────────────────────────────────────────

struct ScoreEntry {
    std::string name;
    int score;
    std::string difficulty;
    int level;
};

// ── LinkedList ───────────────────────────────────────────────────
// Sorted singly linked list (descending by score).
// insertSorted O(n), getTop O(k), getHighest O(1).
// Used for the high-score leaderboard — insertion keeps entries
// ranked so retrieval of the top N needs no extra sort step.

class LinkedList {
    Node<ScoreEntry>* head_;
    int size_;

public:
    LinkedList() : head_(nullptr), size_(0) {}

    ~LinkedList() {
        while (head_) {
            auto* tmp = head_;
            head_ = head_->next;
            delete tmp;
        }
    }

    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;

    void insertSorted(const ScoreEntry& entry) {
        auto* node = new Node<ScoreEntry>(entry);
        if (!head_ || entry.score > head_->data.score) {
            node->next = head_;
            head_ = node;
        } else {
            auto* cur = head_;
            while (cur->next && cur->next->data.score >= entry.score)
                cur = cur->next;
            node->next = cur->next;
            cur->next = node;
        }
        ++size_;
    }

    std::vector<ScoreEntry> getTop(int n) const {
        std::vector<ScoreEntry> out;
        auto* cur = head_;
        for (int i = 0; cur && i < n; ++i, cur = cur->next)
            out.push_back(cur->data);
        return out;
    }

    std::vector<ScoreEntry> toVector() const { return getTop(size_); }

    int getHighest() const { return head_ ? head_->data.score : 0; }
    int size() const { return size_; }
};

// ── Matrix ───────────────────────────────────────────────────────
// 2D integer matrix with Tetris-board helpers.
// clearRow shifts row pointers instead of copying data — O(row)
// pointer swaps vs. O(row × cols) element copies.

class Matrix {
    int** grid_;
    int rows_, cols_;

    void alloc(int fill = 0) {
        grid_ = new int*[rows_];
        for (int r = 0; r < rows_; ++r) {
            grid_[r] = new int[cols_];
            std::memset(grid_[r], fill, cols_ * sizeof(int));
        }
    }

    void dealloc() {
        if (grid_) {
            for (int r = 0; r < rows_; ++r) delete[] grid_[r];
            delete[] grid_;
            grid_ = nullptr;
        }
    }

public:
    Matrix() : grid_(nullptr), rows_(0), cols_(0) {}
    Matrix(int rows, int cols) : rows_(rows), cols_(cols) { alloc(); }
    ~Matrix() { dealloc(); }

    Matrix(const Matrix& o) : rows_(o.rows_), cols_(o.cols_) {
        alloc();
        for (int r = 0; r < rows_; ++r)
            std::memcpy(grid_[r], o.grid_[r], cols_ * sizeof(int));
    }

    Matrix& operator=(const Matrix& o) {
        if (this != &o) {
            dealloc();
            rows_ = o.rows_;
            cols_ = o.cols_;
            alloc();
            for (int r = 0; r < rows_; ++r)
                std::memcpy(grid_[r], o.grid_[r], cols_ * sizeof(int));
        }
        return *this;
    }

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    int get(int r, int c) const {
        if (r >= 0 && r < rows_ && c >= 0 && c < cols_) return grid_[r][c];
        return -1;
    }

    void set(int r, int c, int v) {
        if (r >= 0 && r < rows_ && c >= 0 && c < cols_) grid_[r][c] = v;
    }

    bool isRowFull(int row) const {
        for (int c = 0; c < cols_; ++c)
            if (grid_[row][c] == 0) return false;
        return true;
    }

    void clearRow(int row) {
        delete[] grid_[row];
        for (int r = row; r > 0; --r) grid_[r] = grid_[r - 1];
        grid_[0] = new int[cols_];
        std::memset(grid_[0], 0, cols_ * sizeof(int));
    }

    void clear() {
        for (int r = 0; r < rows_; ++r)
            std::memset(grid_[r], 0, cols_ * sizeof(int));
    }
};

#endif
