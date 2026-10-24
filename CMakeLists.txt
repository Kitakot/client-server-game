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

add_library(reliability
    reliability/reliable_channel.cpp
    reliability/adaptive_timeout.cpp
    reliability/dedup_window.cpp
    reliability/delivery_log.cpp)
target_include_directories(reliability PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(server server/main.cpp)
target_link_libraries(server PRIVATE protocol transport reliability)

add_executable(client client/main.cpp)
target_link_libraries(client PRIVATE protocol telemetry transport)

add_executable(reliable_client client/reliable_client.cpp)
target_link_libraries(reliable_client PRIVATE protocol reliability transport)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

#add_executable(tests
#    tests/protocol_tests.cpp
#    tests/telemetry_tests.cpp
#    tests/test_reliable_channel.cpp
#    tests/test_adaptive_timeout.cpp
#    tests/test_dedup_window.cpp
#    tests/test_delivery_log.cpp
#    tests/test_order_book.cpp)
#target_link_libraries(tests PRIVATE protocol telemetry reliability Catch2::Catch2WithMain)

#enable_testing()
#add_test(NAME tests COMMAND tests)