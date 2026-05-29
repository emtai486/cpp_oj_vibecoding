add_executable(api-server
  src/main.cpp
)

target_compile_features(api-server PRIVATE cxx_std_20)
target_link_libraries(api-server PRIVATE httplib::httplib)

target_compile_definitions(api-server PRIVATE
  API_SERVER_VERSION="${PROJECT_VERSION}"
)

