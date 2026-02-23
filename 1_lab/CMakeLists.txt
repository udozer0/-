set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

add_executable(gas_station main.cpp)

target_link_libraries(gas_station nlohmann_json::nlohmann_json)