cmake_minimum_required(VERSION 3.5.0)
project(race-mpi )
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI)
set(CMAKE_CXX_COMPILER mpic++)

add_executable(race-mpi main.cpp)
target_link_libraries(race-mpi MPI::MPI_CXX)

