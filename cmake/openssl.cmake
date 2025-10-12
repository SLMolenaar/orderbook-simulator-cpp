# OpenSSL dependency configuration
# This file is included by the main CMakeLists.txt
# First tries to find system-installed version, falls back to DEPS_DIR

message(STATUS "Configuring OpenSSL...")

# Try to find system-installed OpenSSL
find_package(OpenSSL QUIET)

if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found in system (version ${OPENSSL_VERSION})")
    message(STATUS "  SSL library: ${OPENSSL_SSL_LIBRARY}")
    message(STATUS "  Crypto library: ${OPENSSL_CRYPTO_LIBRARY}")
else()
    message(STATUS "OpenSSL not found in system")

    # OpenSSL may install libs in lib or lib64 depending on platform
    if(EXISTS "${DEPS_DIR}/lib64/libssl.a")
        set(OPENSSL_LIB_DIR "${DEPS_DIR}/lib64")
    elseif(EXISTS "${DEPS_DIR}/lib/libssl.a")
        set(OPENSSL_LIB_DIR "${DEPS_DIR}/lib")
    else()
        message(FATAL_ERROR
            "OpenSSL not found in system and pre-built version not available.\n"
            "Please either:\n"
            "  1. Install OpenSSL development package (libssl-dev), or\n"
            "  2. Run ./build_deps.sh to build dependencies")
    endif()

    message(STATUS "Using pre-built OpenSSL from ${OPENSSL_LIB_DIR}")

    # Create imported targets for OpenSSL libraries
    add_library(ssl STATIC IMPORTED GLOBAL)
    add_library(crypto STATIC IMPORTED GLOBAL)

    # Configure SSL library
    set_target_properties(ssl PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_LIB_DIR}/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DEPS_DIR}/include"
    )

    # Configure Crypto library
    set_target_properties(crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_LIB_DIR}/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DEPS_DIR}/include"
    )

    # OpenSSL crypto needs pthread and dl on Linux
    if(UNIX AND NOT APPLE)
        target_link_libraries(crypto INTERFACE pthread dl)
    endif()

    # SSL depends on crypto
    target_link_libraries(ssl INTERFACE crypto)

    # Create standard aliases for compatibility
    add_library(OpenSSL::SSL ALIAS ssl)
    add_library(OpenSSL::Crypto ALIAS crypto)
endif()
