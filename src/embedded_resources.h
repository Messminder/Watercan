#pragma once

#include <cstddef>

// When building a single-executable binary we generate a header with all embedded resources
#if defined(BUILD_SINGLE_EXE) || defined(BUILD_WINDOWS_SINGLE_EXE)
#include "embedded/embedded_resources.h"
#endif
