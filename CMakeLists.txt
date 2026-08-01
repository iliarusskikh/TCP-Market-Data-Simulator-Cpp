cmake_minimum_required(VERSION 3.10)
project(TcpMarketDataSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if the user did not pass -DCMAKE_BUILD_TYPE=...
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Tuning for portfolio / demo runs
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g")

add_executable(server src/server.cpp)
add_executable(client src/client.cpp)

target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
