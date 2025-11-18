# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.9)
include(cmake/funcs.cmake)
include(FetchContent)

project(bullet_lmdb)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bullet_db STATIC)

prefix_list(KZG "src/kzg/"
    mp_mf
    mp_sf
    sp_mf
    sp_sf
    pippen
    polynomial
    srs
)

prefix_list(STATE "src/state/"
    branch
    db
    leaf
    ledger
    lru
    path
    utils
)

prefix_list(UTILS "src/utils/" 
    bitmap
    points
    scalar
    key_sig
)

prefix_list(BINDINGS "bindings/" 
    extern
)

set(HEADERS
    ${PROJECT_SOURCE_DIR}/src/kzg/kzg.h

    ${PROJECT_SOURCE_DIR}/src/state/verkle.h
    ${PROJECT_SOURCE_DIR}/src/state/db.h

    ${PROJECT_SOURCE_DIR}/src/utils/utils.h
    ${PROJECT_SOURCE_DIR}/src/utils/bitmap.h
    ${PROJECT_SOURCE_DIR}/src/utils/result.h
    ${PROJECT_SOURCE_DIR}/src/utils/ring_buffer.h

    ${PROJECT_SOURCE_DIR}/bindings/extern.h
)


target_sources(bullet_db
    PRIVATE
        ${UTILS}
        ${STATE}
        ${KZG}
        ${BINDINGS}
    PUBLIC
        ${HEADERS}
)



####################################################
#                   BLST
####################################################
set(BLST_SOURCE ${PROJECT_SOURCE_DIR}/deps/blst)
set(BLST_INCLUDE ${BLST_SOURCE}/bindings)
target_include_directories(bullet_db PUBLIC ${BLST_INCLUDE})
target_link_libraries(bullet_db PUBLIC ${BLST_SOURCE}/libblst.a)


####################################################
#                   BLAKE 3
####################################################
set(BLAKE3_SOURCE ${PROJECT_SOURCE_DIR}/deps/blake3)
set(BLAKE3_INCLUDE ${BLAKE3_SOURCE}/c)
target_include_directories(bullet_db PUBLIC ${BLAKE3_INCLUDE})
target_link_libraries(bullet_db PUBLIC ${BLAKE3_SOURCE}/libblake3.a)


####################################################
#                   LMDB
####################################################
find_path(LMDB_INCLUDE_DIR lmdb.h)
find_library(LMDB_LIBRARY NAMES lmdb)

if(NOT LMDB_INCLUDE_DIR OR NOT LMDB_LIBRARY)
    message(FATAL_ERROR "LMDB not found on the system")
endif()

target_include_directories(bullet_db PRIVATE ${LMDB_INCLUDE_DIR})
target_link_libraries(bullet_db PRIVATE ${LMDB_LIBRARY})


####################################################
#                   TESTING
####################################################
option(TESTING "Build test executables" OFF)
if(TESTING)
    add_subdirectory(tests)
endif()
