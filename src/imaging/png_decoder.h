#pragma once

#include "imaging/image_limits.h"
#include "render/core/image_decoder.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace luciole {
  // Decodes a PNG to straight RGBA; dimensions are checked before any pixels are allocated.
  std::expected<DecodedRasterImage, std::string> decodePng(
      std::span<const std::uint8_t> bytes, std::uint64_t maxPixels = kMaxImagePixels, int maxSide = kMaxImageSide
  );
} // namespace luciole
