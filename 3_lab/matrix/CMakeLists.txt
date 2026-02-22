cmake_minimum_required(VERSION 3.20)
project(matrix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED)

add_executable(multiplication main.cpp)
target_link_libraries(multiplication PRIVATE MPI::MPI_CXX)

add_executable(optimal-multiplication optimal.cpp)
target_link_libraries(optimal-multiplication PRIVATE MPI::MPI_CXX)