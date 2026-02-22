cmake_minimum_required(VERSION 3.5.0)
project(virtual VERSION 0.1.0 LANGUAGES C CXX)

include_directories(..)

find_package(MPI)
set(CMAKE_CXX_COMPILER mpic++)

add_executable(dependency dependency.cpp)
add_executable(signal signal.cpp)
add_executable(line line.cpp)
add_executable(circle circle.cpp)

target_link_libraries(dependency openmpi::openmpi)
target_link_libraries(signal openmpi::openmpi)
target_link_libraries(line openmpi::openmpi)
target_link_libraries(circle openmpi::openmpi)