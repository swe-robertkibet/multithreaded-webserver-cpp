cmake_minimum_required(VERSION 3.16)
project(MultithreadedWebServer VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)
endif()

# Include directories
include_directories(include)

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create executable
add_executable(webserver ${SOURCES})

# Link pthread library
find_package(Threads REQUIRED)
target_link_libraries(webserver Threads::Threads)

# Optional: Add GoogleTest for unit testing
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()
    
    file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
    add_executable(webserver_tests ${TEST_SOURCES} ${SOURCES})
    target_link_libraries(webserver_tests GTest::gtest_main Threads::Threads)
    
    include(GoogleTest)
    gtest_discover_tests(webserver_tests)
endif()

# Set output directory
set_target_properties(webserver PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)