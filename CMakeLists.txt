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
)

# Test executable (functionality and performance tests)
add_executable(OrderBookTests
        tests.cpp
)

# Fetch dependencies
include(FetchContent)

# Fetch nlohmann_json
FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Fetch CURL with SSL support for Windows
FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_4_0
)

# Configure CURL options
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "Use Windows native SSL" FORCE)
set(CMAKE_USE_SCHANNEL ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(curl)

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