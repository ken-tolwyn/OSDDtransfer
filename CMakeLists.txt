cmake_minimum_required(VERSION 3.20)
project(osdd_repository VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(osdd_core
  src/cas.cpp
  src/manifest.cpp
  src/index.cpp
  src/http_server.cpp
)
target_include_directories(osdd_core PUBLIC include)
target_link_libraries(osdd_core PUBLIC OpenSSL::Crypto SQLite::SQLite3)
target_compile_options(osdd_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(osdd-repository src/main.cpp)
target_link_libraries(osdd-repository PRIVATE osdd_core)
target_compile_options(osdd-repository PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
  add_executable(osdd_tests tests/repository_tests.cpp)
  target_link_libraries(osdd_tests PRIVATE osdd_core)
  target_compile_options(osdd_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME repository_core COMMAND osdd_tests)
endif()
