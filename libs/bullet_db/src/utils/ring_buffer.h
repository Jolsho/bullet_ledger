#pragma once
#include <vector>
#include <optional>

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buf_(capacity), head_(0), tail_(0), full_(false), capacity_(capacity){}

    // ----- capacity / state -----
    size_t capacity() const { return capacity_; }
    bool empty() const { return (!full_ && head_ == tail_); }
    bool full() const { return full_; }
    void clear() {
        head_ = 0;
        tail_ = 0;
        full_ = false;
    }

    size_t size() const {
        if (full_) return buf_.size();
        if (head_ >= tail_) return head_ - tail_;
        return buf_.size() - (tail_ - head_);
    }


    // ---- push_back ----
    // adds element at head (logical back)
    void push_back(const T& value) {
        buf_[head_] = value;
        head_ = (head_ + 1) % buf_.size();

        if (full_) {
            tail_ = (tail_ + 1) % buf_.size();
        }
        full_ = head_ == tail_;
    }

    // ---- pop_back ----
    std::optional<T> pop_back() {
        if (empty()) return std::nullopt;

        full_ = false;

        // move head backward
        head_ = (head_ == 0 ? buf_.size() - 1 : head_ - 1);

        return buf_[head_];
    }
    std::optional<T> back() const {
        if (empty()) return std::nullopt;
        size_t idx = (head_ == 0 ? buf_.size() - 1 : head_ - 1);
        return buf_[idx];
    }



    // ---- push_front ----
    void push_front(const T& value) {
        // move tail backward (new front)
        tail_ = (tail_ == 0 ? buf_.size() - 1 : tail_ - 1);
        buf_[tail_] = value;

        if (full_) {
            head_ = (head_ == 0 ? buf_.size() - 1 : head_ - 1);
        }
        full_ = head_ == tail_;
    }

    // ---- pop_front ----
    std::optional<T> pop_front() {
        if (empty()) return std::nullopt;

        full_ = false;

        T value = buf_[tail_];
        tail_ = (tail_ + 1) % buf_.size();

        return value;
    }
    std::optional<T> front() const {
        if (empty()) return std::nullopt;
        return buf_[tail_];
    }

    std::optional<T> get(size_t idx) const {
        if (idx >= size()) return std::nullopt;
        // Logical index -> physical index in buf_
        size_t physical_index = (tail_ + idx) % buf_.size();
        return buf_[physical_index];
    }

private:
    std::vector<T> buf_;
    size_t head_;
    size_t tail_;
    size_t capacity_;
    bool full_;
};
