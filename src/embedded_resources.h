#pragma once

#include <cstddef>

// These symbols are generated at configure time when either BUILD_SINGLE_EXE or BUILD_WINDOWS_SINGLE_EXE is enabled
#if defined(BUILD_SINGLE_EXE) || defined(BUILD_WINDOWS_SINGLE_EXE)
extern const unsigned char embedded_about_image_png[];
extern const std::size_t embedded_about_image_png_len;
#endif
