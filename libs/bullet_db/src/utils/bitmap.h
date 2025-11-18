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

#ifndef BITMAP_H
#define BITMAP_H

#include <array>
#include <cstddef>
#include <cstdint>

class Bitmap {
public:
    static constexpr size_t BITMAP_SIZE = 32 * 8; // 32 bytes = 256 bits

    Bitmap();

    // Check if a bit is set
    bool is_set(size_t bit) const;

    // Set a bit to 1
    void set(size_t bit);

    // Clear a bit to 0
    void clear(size_t bit);

    size_t count();

    // Optional: toggle a bit
    void toggle(size_t bit);

private:
    std::array<uint8_t, 32> data;

    void check_index(size_t bit) const;
};

#endif // BITMAP_H

