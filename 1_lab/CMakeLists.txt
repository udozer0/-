cmake_minimum_required(VERSION 3.5.0)
project(producer-consumer VERSION 0.1.0 LANGUAGES C CXX)

add_compile_definitions(SHARED_MEMORY_NAME="shared")
add_compile_definitions(SHARED_MEMORY_SIZE=2048)
add_compile_definitions(SEMAPHORE_NAME="/semaphoreaaaaar")
add_compile_definitions(CONDITION_VARIABLE_NAME="/condition")

include_directories(shared)

find_package(nlohmann_json REQUIRED)
message(STATUS "nlohmann_json version: ${nlohmann_json_VERSION}")

add_executable(loop src/main.cpp)
target_link_libraries(loop PRIVATE nlohmann_json::nlohmann_json)

