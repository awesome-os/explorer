#pragma once

#include "render/core/image_decoder.h"

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luciole {
  // Private decoder protocol: side, pixel cap, byte cap, MIME type, output path. Input is descriptor 3.
  int runThumbnailWorker(std::span<const std::string_view> arguments);
  // Validate the bounded private RGBA result. Shared on-disk thumbnails remain PNG.
  std::expected<LoadedImageFile, std::string> readThumbnailWorkerResult(std::vector<std::uint8_t> bytes, int maxSide);
} // namespace luciole
