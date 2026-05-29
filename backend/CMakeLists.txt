cmake_minimum_required(VERSION 3.20)

project(ai_native_oj_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(httplib CONFIG QUIET)

if(NOT httplib_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.1
  )
  FetchContent_MakeAvailable(cpp_httplib)
endif()

add_subdirectory(api-server)

