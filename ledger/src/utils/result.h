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
#include <variant>

template <typename T, typename E>
class Result {
private:
    std::variant<T, E> data;

public:
    Result(const T& value) : data(value) {}
    Result(const E& error) : data(error) {}

    bool is_ok() const { return std::holds_alternative<T>(data); }
    bool is_err() const { return std::holds_alternative<E>(data); }

    T& unwrap() { return std::get<T>(data); }
    E& unwrap_err() { return std::get<E>(data); }
};
