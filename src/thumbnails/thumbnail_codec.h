#pragma once

#include "render/core/image_decoder.h"

#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace luciole {
  // Freedesktop thumbnail cache PNG: RGBA pixels plus tEXt metadata.
  struct ThumbnailPng {
    LoadedImageFile image;
    std::map<std::string, std::string> text;
  };
  std::expected<ThumbnailPng, std::string> readThumbnailPng(std::span<const std::uint8_t> bytes, int maxSide);
  std::expected<std::vector<std::uint8_t>, std::string> writeThumbnailPng(const ThumbnailPng& thumbnail);
} // namespace luciole
