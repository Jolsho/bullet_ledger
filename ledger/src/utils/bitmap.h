/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

template <size_t NumBits>
class Bitmap {
public:
    static constexpr size_t BIT_SIZE   = NumBits;
    static constexpr size_t BYTE_SIZE  = NumBits / 8;

    Bitmap(const uint8_t* cursor = nullptr) {
        if (cursor) {
            std::memcpy(data.data(), cursor, BYTE_SIZE);
        } else {
            data.fill(0);
        }
    }

    bool is_set(size_t bit) const {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        return (data[byte_index] & (uint8_t(1) << bit_index)) != 0;
    }

    void set(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] |= (uint8_t(1) << bit_index);
    }

    void clear(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] &= ~(uint8_t(1) << bit_index);
    }

    void toggle(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] ^= (uint8_t(1) << bit_index);
    }

    size_t count() const {
        size_t c = 0;
        for (size_t i = 0; i < BIT_SIZE; i++) {
            if (is_set(i)) c++;
        }
        return c;
    }

    // raw pointer interface
    uint8_t* data_ptr() {
        return data.data();
    }

    const uint8_t* data_ptr() const {
        return data.data();
    }

    // access as array
    const std::array<uint8_t, BYTE_SIZE>& array() const {
        return data;
    }

private:
    std::array<uint8_t, BYTE_SIZE> data;

    void check_index(size_t bit) const {
        if (bit >= BIT_SIZE)
            throw std::out_of_range("Bit index out of range");
    }
};
