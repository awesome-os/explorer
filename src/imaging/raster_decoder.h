#pragma once

#include "render/core/image_decoder.h"

#include <cstdint>
#include <expected>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luciole {
  // Reads a regular file of at most kMaxEncodedImageBytes.
  std::expected<std::vector<std::uint8_t>, std::string> readEncodedImageFile(const std::string& path);
  // Decodes raster bytes through GdkPixbuf, scaled to fit `target`; SVG is rejected.
  std::expected<LoadedImageFile, std::string> decodeRasterImage(
      std::span<const std::uint8_t> bytes, int target, std::uint64_t maxPixels, GCancellable* cancellable,
      std::string_view contentType = {}
  );
  // Some system loaders return full-size pixels despite a requested decode size.
  std::expected<LoadedImageFile, std::string> fitRasterPixels(GdkPixbuf* pixbuf, int target, std::uint64_t maxPixels);
} // namespace luciole
