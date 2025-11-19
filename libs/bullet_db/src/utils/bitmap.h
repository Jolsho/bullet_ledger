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

#include <array>
#include <cstdint>
#include <stdexcept>

class Bitmap {
public:
    static constexpr size_t BITMAP_SIZE = 32 * 8; // 32 bytes = 256 bits

    Bitmap() {
        data.fill(0);
    }

    bool is_set(size_t bit) const {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        return (data[byte_index] & (1 << bit_index)) != 0;
    }

    void set(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] |= (1 << bit_index);
    }

    void clear(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] &= ~(1 << bit_index);
    }

    size_t count() {
        size_t count = 0;
        for (auto i = 0; i < BITMAP_SIZE; i++) {
            if (is_set(i)) count++;
        }
        return count;
    }

    void toggle(size_t bit) {
        check_index(bit);
        size_t byte_index = bit / 8;
        size_t bit_index  = bit % 8;
        data[byte_index] ^= (1 << bit_index);
    }


private:
    std::array<uint8_t, 32> data;

    void check_index(size_t bit) const {
        if (bit >= BITMAP_SIZE) {
            throw std::out_of_range("Bit index out of range");
        }
    }
};
