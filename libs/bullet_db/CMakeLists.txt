# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.9)

project(bullet_lmdb)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bullet_db STATIC)

target_include_directories(bullet_db PUBLIC
    ${PROJECT_SOURCE_DIR}/bindings
)
target_include_directories(bullet_db PRIVATE
    ${PROJECT_SOURCE_DIR}/src/kzg
    ${PROJECT_SOURCE_DIR}/src/state
    ${PROJECT_SOURCE_DIR}/src/utils
    ${PROJECT_SOURCE_DIR}/src/encoding
    ${PROJECT_SOURCE_DIR}/src/bindings
)

file(GLOB_RECURSE HEADERS "src/**/*.h")
file(GLOB_RECURSE SRC "src/**/*.cpp")
target_sources(bullet_db PRIVATE ${SRC} ${HEADERS})


####################################################
#                   BLST
####################################################
set(BLST_SOURCE ${PROJECT_SOURCE_DIR}/deps/blst)
set(BLST_INCLUDE ${BLST_SOURCE}/bindings)
if(TESTING)
    target_include_directories(bullet_db PUBLIC ${BLST_INCLUDE})
    target_link_libraries(bullet_db PUBLIC ${BLST_SOURCE}/libblst.a)
else()
    target_include_directories(bullet_db PRIVATE ${BLST_INCLUDE})
    target_link_libraries(bullet_db PRIVATE ${BLST_SOURCE}/libblst.a)
endif()


####################################################
#                   BLAKE 3
####################################################
set(BLAKE3_SOURCE ${PROJECT_SOURCE_DIR}/deps/blake3)
set(BLAKE3_INCLUDE ${BLAKE3_SOURCE}/c)
if(TESTING)
    target_include_directories(bullet_db PUBLIC ${BLAKE3_INCLUDE})
    target_link_libraries(bullet_db PUBLIC ${BLAKE3_SOURCE}/libblake3.a)
else()
    target_include_directories(bullet_db PRIVATE ${BLAKE3_INCLUDE})
    target_link_libraries(bullet_db PRIVATE ${BLAKE3_SOURCE}/libblake3.a)
endif()


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
