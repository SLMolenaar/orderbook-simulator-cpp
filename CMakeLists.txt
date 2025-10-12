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
        MarketDataFeed.h
        ExchangeRules.h
        CurlClient.h
)

# Test executable (functionality and performance tests)
add_executable(OrderBookTests
        tests.cpp
)

#
# External dependencies
# First tries to use system-installed dependencies
# Falls back to pre-built dependencies from build_deps.sh
#

# Check for ORDERBOOK_DEPS_DIR environment variable
if(DEFINED ENV{ORDERBOOK_DEPS_DIR})
    set(DEPS_DIR $ENV{ORDERBOOK_DEPS_DIR})
    message(STATUS "ORDERBOOK_DEPS_DIR set to: ${DEPS_DIR}")
else()
    # Default to ~/.cache/orderbook-deps/install
    set(DEPS_DIR "$ENV{HOME}/.cache/orderbook-deps/install")
endif()

# Check if pre-built dependencies exist
if(EXISTS "${DEPS_DIR}/.deps_built")
    # Read dependency versions
    file(STRINGS "${DEPS_DIR}/.deps_built" DEPS_INFO)
    message(STATUS "Pre-built dependencies available at ${DEPS_DIR}")
    message(STATUS "  Versions: ${DEPS_INFO}")
else()
    message(STATUS "Pre-built dependencies not found at ${DEPS_DIR}")
    message(STATUS "  Will attempt to use system-installed libraries")
    message(STATUS "  To build dependencies, run: ./build_deps.sh")
endif()

# Include dependency configurations
include(cmake/nlohmann_json.cmake)
include(cmake/openssl.cmake)
include(cmake/libcurl.cmake)

# Live market data executable
add_executable(LiveMarketData
        LiveMarketData.cpp
)

# Link libraries to LiveMarketData
target_link_libraries(LiveMarketData
        CURL::libcurl
        nlohmann_json::nlohmann_json
)

# Enable testing
enable_testing()
add_test(NAME OrderBookTests COMMAND OrderBookTests)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

# Find clang-format
find_program(CLANG_FORMAT "clang-format")

if (CLANG_FORMAT)
    # Get all source files
    file(GLOB_RECURSE ALL_SOURCE_FILES *.cpp *.h)

    # Add custom target for formatting
    add_custom_target(
            format
            COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES}
            COMMENT "Running clang-format"
    )
endif ()