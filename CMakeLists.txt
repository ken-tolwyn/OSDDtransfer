cmake_minimum_required(VERSION 3.16)
project(osddtransfer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)

add_executable(osddtransfer
    src/main.cpp
)

target_link_libraries(osddtransfer PRIVATE yaml-cpp)

target_compile_options(osddtransfer PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS osddtransfer RUNTIME DESTINATION bin)
install(FILES packaging/systemd/osddtransfer.service DESTINATION lib/systemd/user)
install(FILES config/osddtransfer.yaml DESTINATION /etc/osddtransfer)
