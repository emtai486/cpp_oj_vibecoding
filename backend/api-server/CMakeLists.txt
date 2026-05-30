add_executable(api-server
  src/main.cpp
)

find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
  find_path(CPP_HTTPLIB_INCLUDE_DIR httplib.h)
  find_library(CPP_HTTPLIB_LIBRARY NAMES cpp-httplib httplib)
  if(CPP_HTTPLIB_INCLUDE_DIR)
    if(CPP_HTTPLIB_LIBRARY)
      add_library(httplib::httplib UNKNOWN IMPORTED)
      set_target_properties(httplib::httplib PROPERTIES
        IMPORTED_LOCATION "${CPP_HTTPLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CPP_HTTPLIB_INCLUDE_DIR}"
      )
    else()
      add_library(httplib::httplib INTERFACE IMPORTED)
      target_include_directories(httplib::httplib INTERFACE ${CPP_HTTPLIB_INCLUDE_DIR})
    endif()
  else()
    include(FetchContent)
    FetchContent_Declare(
      cpp_httplib
      GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
      GIT_TAG v0.18.1
    )
    FetchContent_MakeAvailable(cpp_httplib)
  endif()
endif()

find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)

target_compile_features(api-server PRIVATE cxx_std_20)
target_include_directories(api-server PRIVATE
  ${CMAKE_SOURCE_DIR}/common
)
target_link_libraries(api-server PRIVATE httplib::httplib OpenSSL::Crypto PostgreSQL::PostgreSQL)

if(WIN32)
  target_link_libraries(api-server PRIVATE ws2_32)
endif()

target_compile_definitions(api-server PRIVATE
  API_SERVER_VERSION="${PROJECT_VERSION}"
)
