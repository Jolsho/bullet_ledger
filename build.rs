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

use std::process::Command;


fn main() {
    let project_dir = "/home/jolsho/Desktop/repos/bullet_ledger/libs/bullet_db".to_string();
    let build_dir = project_dir.clone() + "/build";

    let status = Command::new("cmake")
        .args(&[
            "-S", &project_dir, 
            "-B", &build_dir, 
           // "-DCMAKE_BUILD_TYPE=Release",
        ])
        .status()
        .unwrap();
    assert!(status.success());

    let status = Command::new("cmake")
        .args(&[
            "--build", &build_dir,
          //  "--config", "Release",
        ])
        .status()
        .unwrap();
    assert!(status.success());


    println!("cargo:rustc-link-lib=static=bullet_db");
    println!("cargo:rustc-link-search=native={build_dir}");

    // Link the system LMDB library and c++ std
    println!("cargo:rustc-link-lib=lmdb");
    println!("cargo:rustc-link-lib=dylib=stdc++");

    println!("cargo:rerun-if-changed={}", project_dir);
}
