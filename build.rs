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
