cmake_minimum_required(VERSION 3.10)
project(bullet_lmdb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
set(SOURCES
    bullet_db.cpp
    codes.cpp
)

# Build static library
add_library(bullet_db STATIC ${SOURCES})

# Optionally, link external libraries if needed (e.g., LMDB)
find_library(LMDB_LIB lmdb)
if(LMDB_LIB)
    target_link_libraries(bullet_db PRIVATE ${LMDB_LIB})
endif()

