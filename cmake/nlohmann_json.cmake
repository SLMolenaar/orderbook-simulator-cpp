# nlohmann_json dependency configuration
# This file is included by the main CMakeLists.txt
# First tries to find system-installed version, falls back to DEPS_DIR

message(STATUS "Configuring nlohmann_json...")

# Try to find system-installed nlohmann_json
find_package(nlohmann_json QUIET)

if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json found in system (version ${nlohmann_json_VERSION})")
else()
    message(STATUS "nlohmann_json not found in system")

    # Check if pre-built version exists
    if(NOT EXISTS "${DEPS_DIR}/include/nlohmann/json.hpp")
        message(FATAL_ERROR
            "nlohmann_json not found in system and pre-built version not available.\n"
            "Please either:\n"
            "  1. Install nlohmann_json system-wide, or\n"
            "  2. Run ./build_deps.sh to build dependencies")
    endif()

    message(STATUS "Using pre-built nlohmann_json from ${DEPS_DIR}/include")

    # Create interface library for nlohmann_json (header-only)
    add_library(nlohmann_json INTERFACE IMPORTED GLOBAL)
    set_target_properties(nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DEPS_DIR}/include"
    )

    # Create alias for standard naming convention
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif()
