
cmake_minimum_required(VERSION 3.10)

# Project name and language
project(SystemFileManagerX VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories (adjust if you have an include/ folder)
include_directories(${PROJECT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)

# Collect all source files from src directory
file(GLOB_RECURSE SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# Create executable
add_executable(SystemFileManagerX ${SOURCES})
target_link_libraries(SystemFileManagerX PRIVATE Threads::Threads)

if(WIN32)
  target_compile_definitions(SystemFileManagerX PRIVATE NOMINMAX)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_libraries(SystemFileManagerX PRIVATE stdc++fs)
endif()

install(TARGETS SystemFileManagerX RUNTIME DESTINATION bin)

# If you need extra libraries, link them here
# target_link_libraries(SystemFileManagerX <libname>)

