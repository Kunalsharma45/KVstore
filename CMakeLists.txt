cmake_minimum_required(VERSION 3.14)
project(blazekv CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add headers directory (current dir)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Executable
add_executable(blazekv
    main.cpp
)
