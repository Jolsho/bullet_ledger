cmake_minimum_required(VERSION 3.28)
project(bullet_lmdb)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build static library
add_library(bullet_db STATIC)
target_sources(bullet_db
    PRIVATE
        src/bullet_db.cpp
        src/pnt_sclr.cpp
        src/key_sig.cpp
        src/kzg.cpp
        src/pippen.cpp
        src/path.cpp

        src/bullet_db.h
        src/pnt_sclr.h
        src/key_sig.h
        src/kzg.h
        src/pippen.h
        src/path.h

        bindings/extern.cpp

    PUBLIC
        bindings/extern.h
)
target_include_directories(bullet_db PUBLIC deps/blst/bindings)
target_link_libraries(bullet_db PUBLIC deps/blst/libblst.a)


# Optionally, link external libraries if needed (e.g., LMDB)
find_library(LMDB_LIB lmdb)
target_link_libraries(bullet_db PUBLIC ${LMDB_LIB})
