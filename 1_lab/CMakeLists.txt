cmake_minimum_required(VERSION 3.5.0)
project(producer-consumer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_compile_definitions(SHARED_MEMORY_NAME="shared")
add_compile_definitions(SHARED_MEMORY_SIZE=2048)
add_compile_definitions(SEMAPHORE_NAME="/semaphoreaaaaar")
add_compile_definitions(CONDITION_VARIABLE_NAME="/condition")

include_directories(src)

find_package(nlohmann_json REQUIRED)

add_executable(loop 
    src/main.cpp
    src/shared_memory.cpp
    src/queue.cpp
)

target_link_libraries(loop PRIVATE nlohmann_json::nlohmann_json rt)