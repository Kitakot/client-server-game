cmake_minimum_required(VERSION 3.16)
project(client_server_game)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# protocol — только заголовок, поэтому INTERFACE-библиотека.
add_library(protocol INTERFACE)
target_include_directories(protocol INTERFACE ${CMAKE_SOURCE_DIR})

add_library(telemetry telemetry/telemetry.cpp telemetry/csv_log.cpp)
target_include_directories(telemetry PUBLIC ${CMAKE_SOURCE_DIR})

add_library(transport transport/udp_socket.cpp)
target_include_directories(transport PUBLIC ${CMAKE_SOURCE_DIR})
if (WIN32)
    target_link_libraries(transport PUBLIC ws2_32)
endif()

add_executable(server server/main.cpp)
target_link_libraries(server PRIVATE protocol transport)

add_executable(client client/main.cpp)
target_link_libraries(client PRIVATE protocol telemetry transport)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(tests tests/protocol_tests.cpp tests/telemetry_tests.cpp)
target_link_libraries(tests PRIVATE protocol telemetry Catch2::Catch2WithMain)

enable_testing()
add_test(NAME tests COMMAND tests)