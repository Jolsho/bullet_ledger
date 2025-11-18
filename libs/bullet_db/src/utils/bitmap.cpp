// SPDX-License-Identifier: GPL-2.0-only

#include "bitmap.h"
#include <cstddef>
#include <stdexcept>

Bitmap::Bitmap() {
    data.fill(0);
}

bool Bitmap::is_set(size_t bit) const {
    check_index(bit);
    size_t byte_index = bit / 8;
    size_t bit_index  = bit % 8;
    return (data[byte_index] & (1 << bit_index)) != 0;
}

void Bitmap::set(size_t bit) {
    check_index(bit);
    size_t byte_index = bit / 8;
    size_t bit_index  = bit % 8;
    data[byte_index] |= (1 << bit_index);
}

void Bitmap::clear(size_t bit) {
    check_index(bit);
    size_t byte_index = bit / 8;
    size_t bit_index  = bit % 8;
    data[byte_index] &= ~(1 << bit_index);
}

size_t Bitmap::count() {
    size_t count = 0;
    for (auto i = 0; i < BITMAP_SIZE; i++) {
        if (is_set(i)) count++;
    }
    return count;
}

void Bitmap::toggle(size_t bit) {
    check_index(bit);
    size_t byte_index = bit / 8;
    size_t bit_index  = bit % 8;
    data[byte_index] ^= (1 << bit_index);
}

void Bitmap::check_index(size_t bit) const {
    if (bit >= BITMAP_SIZE) {
        throw std::out_of_range("Bit index out of range");
    }
}

