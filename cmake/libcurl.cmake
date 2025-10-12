# libcurl dependency configuration
# This file is included by the main CMakeLists.txt
# First tries to find system-installed version, falls back to DEPS_DIR
# Depends on OpenSSL being configured first

message(STATUS "Configuring libcurl...")

# Try to find system-installed libcurl
find_package(CURL QUIET)

if(CURL_FOUND)
    message(STATUS "libcurl found in system (version ${CURL_VERSION_STRING})")
    message(STATUS "  Include directory: ${CURL_INCLUDE_DIR}")
    message(STATUS "  Library: ${CURL_LIBRARIES}")

    # Create alias if it doesn't exist
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS CURL::CURL)
    endif()
else()
    message(STATUS "libcurl not found in system")

    # Check if pre-built version exists
    if(NOT EXISTS "${DEPS_DIR}/lib/libcurl.a")
        message(FATAL_ERROR
            "libcurl not found in system and pre-built version not available.\n"
            "Please either:\n"
            "  1. Install libcurl development package (libcurl4-openssl-dev), or\n"
            "  2. Run ./build_deps.sh to build dependencies")
    endif()

    message(STATUS "Using pre-built libcurl from ${DEPS_DIR}/lib")

    # Create imported target for libcurl
    add_library(libcurl STATIC IMPORTED GLOBAL)
    set_target_properties(libcurl PROPERTIES
        IMPORTED_LOCATION "${DEPS_DIR}/lib/libcurl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DEPS_DIR}/include"
    )

    # libcurl depends on OpenSSL
    target_link_libraries(libcurl INTERFACE OpenSSL::SSL OpenSSL::Crypto)

    # libcurl needs pthread on Linux
    if(UNIX AND NOT APPLE)
        target_link_libraries(libcurl INTERFACE pthread)
    endif()

    # Create standard alias for compatibility
    add_library(CURL::libcurl ALIAS libcurl)
endif()
