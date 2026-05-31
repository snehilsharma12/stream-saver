#pragma once

#include <cstdint>
#include <string>

namespace stream_saver {

std::string rgba_to_png_base64(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t stride);

} // namespace stream_saver
