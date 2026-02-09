cmake_minimum_required(VERSION 3.16)

# Workaround for older CMake min versions in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(Watercan VERSION 1.6 LANGUAGES CXX C)

# Build option: attempt a single-binary build by preferring static libs where possible.
# This is a best-effort option: OpenGL (libGL) will remain dynamic to preserve compatibility.
option(BUILD_SINGLE_BINARY "Attempt to build a single binary (static libs where possible, GL dynamic)" OFF)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
else()
    set(PLATFORM_LINUX TRUE)
endif()

# Fetch dependencies
include(FetchContent)

# GLFW for window management
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
# Prefer building static lib for GLFW when requesting a single-binary build (best-effort)
if(BUILD_SINGLE_BINARY)
    set(GLFW_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui (docking branch for docking support)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)


# stb_image for image loading (header-only)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Detect SDL2 for audio support (used by MusicPlayer)
find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 sdl2)
endif()
if (SDL2_FOUND)
    add_compile_definitions(HAVE_SDL2)
    include_directories(${SDL2_INCLUDE_DIRS})
    list(APPEND EXTRA_LIBS ${SDL2_LIBRARIES})
    message(STATUS "SDL2 found: audio support enabled")
endif()

# ImGui library
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# Main executable
add_executable(Watercan
    src/main.cpp
    src/app.cpp
    src/spirit_tree.cpp
    src/tree_renderer.cpp
    src/stb_image_impl.cpp
    src/app_type_colors.cpp
    src/TextEditor.cpp
    src/music_player.cpp
    build/_deps/stb-src/stb_vorbis.c
)

target_include_directories(Watercan PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

target_link_libraries(Watercan PRIVATE 
    imgui_lib 
    nlohmann_json::nlohmann_json
    glfw
    OpenGL::GL
)

# Link extra optional libraries (SDL2 etc.)
if(EXTRA_LIBS)
    target_link_libraries(Watercan PRIVATE ${EXTRA_LIBS})
endif()

# If requested, attempt to make linker prefer static runtime libs (best-effort).
if(BUILD_SINGLE_BINARY)
    message(STATUS "Building single-binary (best-effort): enabling -static-libgcc -static-libstdc++")
    target_link_options(Watercan PRIVATE -static-libgcc -static-libstdc++)
endif()

# Platform-specific settings
if(PLATFORM_WINDOWS)
    target_compile_definitions(Watercan PRIVATE PLATFORM_WINDOWS)
    # Hide console window on Windows for release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(Watercan PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
elseif(PLATFORM_MACOS)
    target_compile_definitions(Watercan PRIVATE PLATFORM_MACOS)
    target_link_libraries(Watercan PRIVATE "-framework Cocoa" "-framework IOKit")
else()
    target_compile_definitions(Watercan PRIVATE PLATFORM_LINUX)
endif()

# Options: build a single exe embedding assets
option(BUILD_WINDOWS_SINGLE_EXE "Embed assets into the executable for Windows builds" OFF)
option(BUILD_SINGLE_EXE "Embed assets into the executable for native builds (Linux)" OFF)

# If building single exe (Windows or Linux), generate embedded resource sources at configure time
if(BUILD_WINDOWS_SINGLE_EXE OR BUILD_SINGLE_EXE)
    # Generate C++ source files that contain the image as a byte array.
    # We use a simple CMake-based utility to create the arrays.
    function(embed_file_as_cpp IN_PATH OUT_CPP VAR_NAME)
        file(READ "${IN_PATH}" FILE_CONTENTS HEX)
        # Convert binary to C array literal
        file(WRITE "${OUT_CPP}" "#include <cstddef>\nextern const unsigned char ${VAR_NAME}[];\nextern const std::size_t ${VAR_NAME}_len;\n\nconst unsigned char ${VAR_NAME}[] = {")
        # Iterate bytes
        file(READ "${IN_PATH}" _BIN HEXBIN HEX)
            # Use xxd if available for robust binary dumping
        find_program(XXD_EXECUTABLE xxd)
        if(XXD_EXECUTABLE)
            execute_process(COMMAND ${XXD_EXECUTABLE} -i "${IN_PATH}" OUTPUT_VARIABLE XOUT)
            # xxd -i prints a C array; capture it and adapt to our naming
            string(REGEX REPLACE "unsigned char .*\[.*\] =" "const unsigned char ${VAR_NAME}[] =" XOUT "${XOUT}")
            # xxd also provides length variable name like "filename_len". Normalize it
            string(REGEX REPLACE "(size_t) .*_len" "const std::size_t ${VAR_NAME}_len" XOUT "${XOUT}")
            file(WRITE "${OUT_CPP}" "${XOUT}")
        else()
            # Fallback to Python script if xxd is not available
            find_program(PYTHON3_EXECUTABLE python3 python)
            if(PYTHON3_EXECUTABLE)
                execute_process(COMMAND ${PYTHON3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_bin.py" "${IN_PATH}" "${VAR_NAME}" "${OUT_CPP}")
            else()
                message(FATAL_ERROR "xxd not found and python3 not available; required to embed binary assets. Install xxd (vim-common) or python3, or disable BUILD_SINGLE_EXE or BUILD_WINDOWS_SINGLE_EXE.")
            endif()
        endif()
    endfunction()

    set(EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
    file(MAKE_DIRECTORY ${EMBED_DIR})

    # Embed all files under res/ into the executable. Generate C++ sources and a header with externs.
    file(GLOB EMBED_INPUT_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/res" "${CMAKE_CURRENT_SOURCE_DIR}/res/*")
    set(EXTRA_EMBEDDED_SOURCES)
    set(EMBED_VAR_NAMES)
    foreach(f IN LISTS EMBED_INPUT_FILES)
        set(IN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res/${f}")
        # Sanitize variable name: replace non-alnum with underscore
        string(REGEX REPLACE "[^A-Za-z0-9]" "_" VAR_BASE "${f}")
        # Preserve historical name for About image so existing code symbols remain stable
        if("${f}" STREQUAL "TheBrokenClip.png")
            set(VAR_NAME "embedded_about_image_png")
        else()
            set(VAR_NAME "embedded_${VAR_BASE}")
        endif()
        set(OUT_CPP "${EMBED_DIR}/${VAR_NAME}.cpp")
        embed_file_as_cpp(${IN_PATH} ${OUT_CPP} ${VAR_NAME})
        list(APPEND EXTRA_EMBEDDED_SOURCES ${OUT_CPP})
        list(APPEND EMBED_VAR_NAMES ${VAR_NAME})
    endforeach()

    # Generate header with extern declarations (placed in embedded/ subdir so include path matches)
    file(MAKE_DIRECTORY "${EMBED_DIR}/embedded")
    set(EMBED_HEADER "${EMBED_DIR}/embedded/embedded_resources.h")
    file(WRITE ${EMBED_HEADER} "#pragma once\n#include <cstddef>\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
    foreach(var IN LISTS EMBED_VAR_NAMES)
        file(APPEND ${EMBED_HEADER} "extern const unsigned char ${var}[];\nextern const std::size_t ${var}_len;\n\n")
    endforeach()
    # Declare runtime accessor: returns pointer to data and sets length, or nullptr if not found
    file(APPEND ${EMBED_HEADER} "// Return pointer to embedded resource data by filename (UTF-8).\n")
    file(APPEND ${EMBED_HEADER} "// If found, returns pointer and writes its length into *out_len, otherwise returns nullptr.\n")
    file(APPEND ${EMBED_HEADER} "const unsigned char* embedded_resource_data(const char* filename, size_t* out_len);\n\n")
    file(APPEND ${EMBED_HEADER} "#ifdef __cplusplus\n}\n#endif\n")

    # Generate C++ accessor that maps filenames (relative to res/) to embedded symbols
    set(EMBED_CPP "${EMBED_DIR}/embedded/embedded_resources.cpp")
    file(WRITE ${EMBED_CPP} "#include \"embedded_resources.h\"\n#include <string.h>\n\nconst unsigned char* embedded_resource_data(const char* filename, size_t* out_len) {\n    if (!filename) return nullptr;\n")
    list(LENGTH EMBED_INPUT_FILES EMBED_FILES_COUNT)
    math(EXPR EMBED_LAST_INDEX "${EMBED_FILES_COUNT} - 1")
    if (EMBED_FILES_COUNT GREATER 0)
        foreach(i RANGE 0 ${EMBED_LAST_INDEX})
            list(GET EMBED_INPUT_FILES ${i} EMBED_FNAME)
            list(GET EMBED_VAR_NAMES ${i} EMBED_VNAME)
            # Escape backslashes (Windows paths) and quotes in filename
            string(REPLACE "\\" "\\\\" EMBED_FNAME_ESCAPED "${EMBED_FNAME}")
            file(APPEND ${EMBED_CPP} "    if (strcmp(filename, \"${EMBED_FNAME_ESCAPED}\") == 0) { if (out_len) *out_len = ${EMBED_VNAME}_len; return ${EMBED_VNAME}; }\n")
        endforeach()
    endif()
    file(APPEND ${EMBED_CPP} "    // Not found\n    if (out_len) *out_len = 0;\n    return nullptr;\n}\n")

    # Add generated accessor to sources so it's compiled into the executable
    list(APPEND EXTRA_EMBEDDED_SOURCES ${EMBED_CPP})

    # Generate C++ source implementing the accessor that maps filenames to symbols
    set(EMBED_CPP "${EMBED_DIR}/embedded/embedded_resources.cpp")
    file(WRITE ${EMBED_CPP} "#include \"embedded_resources.h\"\n#include <string.h>\n\nconst unsigned char* embedded_resource_data(const char* filename, size_t* out_len) {\n    if (!filename) return nullptr;\n")
    # Append checks for each file/var mapping
    foreach(idx RANGE 0 ${EMBED_INPUT_FILES_LENGTH})
        # NOTE: We'll fill this area in later; placeholder for loop below
    endforeach()
    file(APPEND ${EMBED_CPP} "    // Not found\n    if (out_len) *out_len = 0;\n    return nullptr;\n}\n")

    # Add embedded sources to target
    if(EXTRA_EMBEDDED_SOURCES)
        target_sources(Watercan PRIVATE ${EXTRA_EMBEDDED_SOURCES})
    endif()

    # Ensure compile-time flag present for embedded assets
    if(BUILD_WINDOWS_SINGLE_EXE)
        target_compile_definitions(Watercan PRIVATE BUILD_WINDOWS_SINGLE_EXE=1)
    endif()
    if(BUILD_SINGLE_EXE)
        target_compile_definitions(Watercan PRIVATE BUILD_SINGLE_EXE=1)
    endif()

    # Ensure the build embedded dir is in include path so generated header can be found via "embedded/embedded_resources.h"
    target_include_directories(Watercan PRIVATE ${EMBED_DIR})
endif()

# No build-time dependency on sample JSON files; the application loads JSON files at runtime via File > Open or from the working directory if provided.
