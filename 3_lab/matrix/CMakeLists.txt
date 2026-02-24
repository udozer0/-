cmake_minimum_required(VERSION 3.20)
project(matrix_all LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED)

add_executable(matrix matrix.cpp)
target_link_libraries(matrix PRIVATE MPI::MPI_CXX)

add_executable(matrix-simple matrix-simple.cpp)
target_link_libraries(matrix-simple PRIVATE MPI::MPI_CXX)