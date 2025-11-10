# Gather test sources
set(TEST_SOURCES
    main.cpp
    key_sig.cpp
    multi_kzg.cpp
    full_kzg.cpp
    single_kzg.cpp
    tests.h
)

# Create a test executable
add_executable(tests_exec ${TEST_SOURCES})

# Link the main library
target_link_libraries(tests_exec PRIVATE bullet_db)
target_include_directories(tests_exec PRIVATE 
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/kzg
        ${PROJECT_SOURCE_DIR}/src/state
        ${PROJECT_SOURCE_DIR}/src/utils
)

# Register with CTest
add_test(NAME bullet_tests COMMAND tests_exec)

