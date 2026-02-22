cmake_minimum_required(VERSION 3.5.0)
project(test-mpi VERSION 0.1.0 LANGUAGES C CXX)
message(STATUS ${CONAN_ENV_SCRIPT})

find_package(MPI REQUIRED) 

set(CMAKE_CXX_COMPILER mpic++)


add_executable(test-mpi main.cpp)

target_link_libraries(test-mpi MPI::MPI_CXX)


