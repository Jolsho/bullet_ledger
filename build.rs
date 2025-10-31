
fn main() {
    cc::Build::new()
        .cpp(true)
        .include("libs/bullet_db")
        .files([
            "libs/bullet_db/bullet_db.cpp",
            "libs/bullet_db/extern.cpp"
        ])
        .flag_if_supported("-std=c++17")
        .compile("bullet_db");

    // println!("cargo:rustc-link-lib=static=bullet_db");
    // println!("cargo:rustc-link-search=native=/home/jolsho/Desktop/repos/bullet_ledger/libs/bullet_db/build");

    // Link the system LMDB library
    println!("cargo:rustc-link-lib=lmdb");
    println!("cargo:rerun-if-changed=src/bullet_db.cpp");
    println!("cargo:rerun-if-changed=src/bullet_db.h");
}
