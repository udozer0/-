cmake_minimum_required(VERSION 3.20)
project(race LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ftxui CONFIG REQUIRED)
add_compile_definitions(MQ_NAME="/mq")

add_executable(race src/main.cpp)

target_link_libraries(race PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)