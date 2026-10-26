cmake_minimum_required(VERSION 3.22.1)
project(rubik_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rubik_solver SHARED
    native_solver.cpp
    engine/cube/cube.cpp
    engine/cube/notation.cpp
    engine/cube/scramble.cpp
    engine/solver/coords.cpp
    engine/solver/move_tables.cpp
    engine/solver/pruning.cpp
    engine/solver/symmetry.cpp
    engine/solver/ida.cpp
    engine/cube_nxn/cube_nxn.cpp
    engine/cube_nxn/reduction_general.cpp
)

target_include_directories(rubik_solver PRIVATE engine)
target_compile_options(rubik_solver PRIVATE -O2 -Wall -Wextra)
