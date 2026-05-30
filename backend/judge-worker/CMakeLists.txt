cmake_minimum_required(VERSION 3.20)

project(ai_native_oj_judge_worker VERSION 0.1.0 LANGUAGES CXX)

add_executable(judge-worker
  src/main.cpp
)

find_package(PostgreSQL REQUIRED)

target_compile_features(judge-worker PRIVATE cxx_std_20)
target_include_directories(judge-worker PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../common
)
target_link_libraries(judge-worker PRIVATE PostgreSQL::PostgreSQL)

if(WIN32)
  target_link_libraries(judge-worker PRIVATE ws2_32)
endif()
