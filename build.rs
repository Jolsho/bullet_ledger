
fn main() {
    cc::Build::new()
        .cpp(true)
        .file("lmdb/lmdb_wrapper.cpp")
        .flag_if_supported("-std=c++17")
        .compile("lmdb_wrapper");

    // Link the system LMDB library
    println!("cargo:rustc-link-lib=lmdb");
    println!("cargo:rerun-if-changed=src/lmdb_wrapper.cpp");
}
