// The one thing the two SD backends differ in, for shared/sd_files.cpp.  Not part of the API.
#pragma once

#include <cstddef>

#include "clk/status.hpp"

namespace clk::hal::sd::detail {

// Turn an API path ("/sd/tones/x.wav") into one the C library can open: the same string on
// target (FATFS is mounted at /sd in the VFS), a directory of the host's choosing on the host.
// NotPresent when there is no mounted card; BadArg for a path outside /sd.
Status native_path(const char* path, char* out, std::size_t cap) noexcept;

}  // namespace clk::hal::sd::detail
