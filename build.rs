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

fn main() {
    let project_dir = "/home/jolsho/repos/bullet_ledger/ledger".to_string();
    let build_dir = project_dir.clone() + "/build";

    println!("cargo:rustc-link-lib=dylib=bullet_db");
    println!("cargo:rustc-link-search=native={}", build_dir);

    println!("cargo:rustc-link-lib=dylib=stdc++");
}
