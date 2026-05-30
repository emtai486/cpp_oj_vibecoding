cmake_minimum_required(VERSION 3.20)

project(ai_native_oj_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(api-server)
add_subdirectory(judge-worker)
