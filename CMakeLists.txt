cmake_minimum_required(VERSION 3.14)
project(JokerProtocol VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add Include Directory
include_directories(include)

# Joker Core Library
add_library(joker_core
    src/mac_address.cpp
    src/crc32.cpp
    src/header.cpp
    src/dedup_cache.cpp
    src/neighbor.cpp
    src/routing.cpp
    src/ogm.cpp
    src/candidate.cpp
    src/timer_wheel.cpp
    src/coordinator_timer.cpp
    src/coordinator_ack.cpp
    src/forwarding.cpp
    src/cmsi.cpp
    src/metrics.cpp
    adapters/mock/mock_adapter.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(joker_core PRIVATE adapters/linux_af_packet/linux_af_packet.cpp)
endif()

target_include_directories(joker_core PUBLIC include .)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

add_executable(joker_tests
    tests/unit/mac_address_test.cpp
    tests/unit/crc32_test.cpp
    tests/unit/mock_adapter_test.cpp
    tests/unit/header_test.cpp
    tests/unit/dedup_cache_test.cpp
    tests/unit/routing_test.cpp
    tests/unit/neighbor_test.cpp
    tests/unit/candidate_test.cpp
    tests/unit/timer_wheel_test.cpp
    tests/unit/coordinator_timer_test.cpp
    tests/unit/coordinator_ack_test.cpp
    tests/unit/forwarding_test.cpp
    tests/unit/cmsi_test.cpp
    tests/unit/metrics_test.cpp
)
target_link_libraries(joker_tests PRIVATE joker_core GTest::gtest_main)

# Integration Tests
add_executable(joker_integration_tests
    tests/integration/integration_test.cpp
    tests/integration/penetration_test.cpp
)
target_link_libraries(joker_integration_tests PRIVATE joker_core GTest::gtest_main)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(joker_tests)
gtest_discover_tests(joker_integration_tests)
