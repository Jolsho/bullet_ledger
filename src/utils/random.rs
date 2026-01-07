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


use rand::{rngs::OsRng, TryRngCore};

pub fn random_b32() -> [u8; 32] {
    let mut buff = [0u8; 32];
    OsRng.try_fill_bytes(&mut buff).unwrap();
    buff
}

pub fn random_b2() -> [u8; 2] {
    let mut buff = [0u8; 2];
    OsRng.try_fill_bytes(&mut buff).unwrap();
    buff
}
