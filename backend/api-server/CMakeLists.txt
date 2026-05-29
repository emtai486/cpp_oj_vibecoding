add_executable(api-server
  src/main.cpp
)

find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)

target_compile_features(api-server PRIVATE cxx_std_20)
target_link_libraries(api-server PRIVATE httplib::httplib OpenSSL::Crypto PostgreSQL::PostgreSQL)

target_compile_definitions(api-server PRIVATE
  API_SERVER_VERSION="${PROJECT_VERSION}"
)
