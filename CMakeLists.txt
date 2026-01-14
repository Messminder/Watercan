cmake_minimum_required(VERSION 3.16)

# Workaround for older CMake min versions in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")

project(Watercan VERSION 1.0.0 LANGUAGES CXX C)

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

# Packaging: create a standalone Windows folder with the built exe and runtime assets
if(WIN32)
    # Create a packaging target that only copies files that exist to avoid failures
    add_custom_target(package_windows DEPENDS Watercan COMMENT "Packaging Standalone Windows bundle into Standalone/Windows")

    add_custom_command(TARGET package_windows POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/Standalone/Windows"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:Watercan> "${CMAKE_SOURCE_DIR}/Standalone/Windows/Watercan.exe"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "${CMAKE_SOURCE_DIR}/Standalone/Windows/res"
    )

    if(EXISTS "${CMAKE_SOURCE_DIR}/README.md")
        add_custom_command(TARGET package_windows POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/README.md" "${CMAKE_SOURCE_DIR}/Standalone/Windows/README.md"
        )
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
        add_custom_command(TARGET package_windows POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/Standalone/Windows/LICENSE"
        )
    endif()
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
)

# Add WinMain wrapper on Windows to suppress console in Release
if(WIN32)
    target_sources(Watercan PRIVATE src/winmain_wrapper.cpp)
    # Use WIN32 subsystem only for Release
    set_property(TARGET Watercan PROPERTY WIN32_EXECUTABLE $<$<CONFIG:Release>:TRUE>)
endif()

target_include_directories(Watercan PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${stb_SOURCE_DIR}
)

# If embedding assets, define compile-time flag (sources are appended below after generation)
if(BUILD_WINDOWS_SINGLE_EXE)
    target_compile_definitions(Watercan PRIVATE BUILD_WINDOWS_SINGLE_EXE=1)
endif()

target_link_libraries(Watercan PRIVATE 
    imgui_lib 
    nlohmann_json::nlohmann_json
    glfw
    OpenGL::GL
)

# If requested, attempt to make linker prefer static runtime libs (best-effort).
if(BUILD_SINGLE_BINARY)
    message(STATUS "Building single-binary (best-effort): enabling -static-libgcc -static-libstdc++")
    target_link_options(Watercan PRIVATE -static-libgcc -static-libstdc++)
endif()

# Platform-specific settings
if(PLATFORM_WINDOWS)
    target_compile_definitions(Watercan PRIVATE PLATFORM_WINDOWS)
    # Keep console subsystem to use standard main() entrypoint on Windows.
    # If you want a GUI-only executable (no console), change this to set
    # WIN32_EXECUTABLE for the appropriate configuration and provide WinMain.
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

    # Embed About image only (JSON will not be embedded in the exe)
    set(EMBED_IMG_CPP "${EMBED_DIR}/embedded_about_image.cpp")
    embed_file_as_cpp(${CMAKE_CURRENT_SOURCE_DIR}/res/TheBrokenMind.png ${EMBED_IMG_CPP} embedded_about_image_png)

    # Add embedded image source to list and include in the target
    list(APPEND EXTRA_EMBEDDED_SOURCES ${EMBED_IMG_CPP})
    target_sources(Watercan PRIVATE ${EMBED_IMG_CPP})

    # Ensure compile-time flag present for embedded assets
    if(BUILD_WINDOWS_SINGLE_EXE)
        target_compile_definitions(Watercan PRIVATE BUILD_WINDOWS_SINGLE_EXE=1)
    endif()
    if(BUILD_SINGLE_EXE)
        target_compile_definitions(Watercan PRIVATE BUILD_SINGLE_EXE=1)
    endif()
endif()

# No build-time dependency on sample JSON files; the application loads JSON files at runtime via File > Open or from the working directory if provided.
