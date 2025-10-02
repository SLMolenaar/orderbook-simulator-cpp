cmake_minimum_required(VERSION 3.10)
project(OrderBookSim)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Header files
set(HEADERS
        OrderType.h
        Types.h
        LevelInfo.h
        Order.h
        OrderModify.h
        Trade.h
        OrderBook.h
        Constants.h
)

# Main executable (original program)
add_executable(OrderBookSim
        main.cpp
)

# Test executable (separate program)
add_executable(OrderBookTests
        tests.cpp
)

# Enable testing
enable_testing()
add_test(NAME OrderBookTests COMMAND OrderBookTests)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")