#include "thumbnails/thumbnail_codec.h"

#include "fs/gio_ptr.h"
#include "imaging/png_decoder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <gio/gio.h>
#include <png.h>

namespace {
  constexpr std::size_t kMaxText = 131072;
  std::expected<std::string, std::string> textValue(std::span<const std::uint8_t> data, bool compressed) {
    if (!compressed) {
      if (data.size() > kMaxText) {
        return std::unexpected("oversized PNG metadata");
      }
      return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }
    const luciole::GioPtr<GZlibDecompressor> decoder(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB));
    std::string value(kMaxText + 1, '\0');
    gsize consumed = 0, produced = 0;
    const auto result = g_converter_convert(
        G_CONVERTER(decoder.get()), data.data(), data.size(), value.data(), value.size(), G_CONVERTER_INPUT_AT_END,
        &consumed, &produced, nullptr
    );
    if (result != G_CONVERTER_FINISHED || consumed != data.size() || produced > kMaxText) {
      return std::unexpected("invalid or oversized compressed PNG metadata");
    }
    value.resize(produced);
    return value;
  }
  std::uint32_t read32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
        | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8)
        | p[3];
  }
  std::uint32_t pngCrc(std::span<const std::uint8_t> bytes) {
    static constexpr auto table = [] {
      std::array<std::uint32_t, 256> values{};
      for (std::uint32_t value = 0; value < values.size(); ++value) {
        auto crc = value;
        for (int bit = 0; bit < 8; ++bit) {
          crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
        values[value] = crc;
      }
      return values;
    }();
    std::uint32_t crc = 0xffffffffU;
    for (auto byte : bytes) {
      crc = table[(crc ^ byte) & 255U] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffU;
  }
} // namespace

std::expected<luciole::ThumbnailPng, std::string>
luciole::readThumbnailPng(std::span<const std::uint8_t> bytes, int maxSide) {
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
  if (maxSide < 1
      || maxSide > 1024
      || bytes.size() < 45
      || bytes.size() > 8U * 1024U * 1024U
      || !std::ranges::equal(bytes.first(8), signature)) {
    return std::unexpected("invalid thumbnail PNG");
  }
  ThumbnailPng result;
  bool ended = false, filter = false;
  std::size_t textBytes = 0;
  const auto pixelChunk = [](std::string_view type, std::uint32_t length) {
    return type == "IHDR"
        || type == "IDAT"
        || type == "PLTE"
        || type == "tRNS"
        || type == "IEND"
        || (type == "gAMA" && length == 4)
        || (type == "sRGB" && length == 1)
        || (type == "cHRM" && length == 32);
  };
  for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
    const auto* p = bytes.data() + offset;
    const auto length = read32(p);
    if (length > bytes.size() - offset - 12
        || read32(p + 8 + length) != pngCrc(bytes.subspan(offset + 4, static_cast<std::size_t>(length) + 4))) {
      return std::unexpected("invalid PNG chunk");
    }
    const std::string_view type(reinterpret_cast<const char*>(p + 4), 4);
    if (offset == 8 && (type != "IHDR" || length != 13)) {
      return std::unexpected("missing PNG header");
    }
    if (type == "tEXt" || type == "zTXt" || type == "iTXt") {
      if (length > kMaxText) {
        return std::unexpected("oversized PNG metadata");
      }
      const auto data = bytes.subspan(offset + 8, length);
      const auto split = std::ranges::find(data, 0);
      const auto keySize = static_cast<std::size_t>(split - data.begin());
      if (split == data.end() || keySize == 0 || keySize > 79 || result.text.size() >= 64) {
        return std::unexpected("invalid PNG metadata");
      }
      auto valueBytes = data.subspan(keySize + 1);
      bool compressed = false;
      if (type == "zTXt") {
        if (valueBytes.empty() || valueBytes.front() != 0) {
          return std::unexpected("invalid PNG text compression");
        }
        compressed = true;
        valueBytes = valueBytes.subspan(1);
      } else if (type == "iTXt") {
        if (valueBytes.size() < 2 || valueBytes[0] > 1 || valueBytes[1] != 0) {
          return std::unexpected("invalid international PNG text");
        }
        compressed = valueBytes[0] != 0;
        valueBytes = valueBytes.subspan(2);
        for (int field = 0; field < 2; ++field) {
          const auto end = std::ranges::find(valueBytes, 0);
          if (end == valueBytes.end()) {
            return std::unexpected("invalid international PNG text");
          }
          valueBytes = valueBytes.subspan(static_cast<std::size_t>(end - valueBytes.begin()) + 1);
        }
      }
      auto value = textValue(valueBytes, compressed);
      if (!value || value->contains('\0') || textBytes + value->size() > 256U * 1024U) {
        return std::unexpected("invalid or oversized PNG metadata");
      }
      if (type == "iTXt" && !g_utf8_validate(value->data(), static_cast<gssize>(value->size()), nullptr)) {
        return std::unexpected("invalid international PNG text");
      }
      textBytes += value->size();
      if (!result.text.emplace(std::string(reinterpret_cast<const char*>(data.data()), keySize), std::move(*value))
               .second) {
        return std::unexpected("duplicate PNG metadata");
      }
      filter = filter || type != "tEXt";
    } else if (!pixelChunk(type, length)) {
      if ((p[4] & 32U) == 0) {
        return std::unexpected("unsupported critical PNG chunk");
      }
      filter = true;
    }
    offset += static_cast<std::size_t>(length) + 12;
    if (type == "IEND") {
      ended = length == 0 && offset == bytes.size();
      break;
    }
  }
  if (!ended) {
    return std::unexpected("missing PNG end");
  }
  // Ordinary shared-cache PNGs need no copy. Strip potentially compressed ancillary metadata only when present.
  std::vector<std::uint8_t> pixels;
  if (filter) {
    pixels.assign(signature.begin(), signature.end());
    pixels.reserve(bytes.size());
    for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
      const auto length = read32(bytes.data() + offset);
      const std::string_view type(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
      const auto end = offset + length + 12;
      if (pixelChunk(type, length)) {
        pixels.insert(
            pixels.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(end)
        );
      }
      offset = end;
    }
    bytes = pixels;
  }
  const auto side = static_cast<std::uint64_t>(maxSide);
  auto decoded = decodePng(bytes, side * side, maxSide);
  if (!decoded) {
    return std::unexpected("oversized or corrupt thumbnail PNG");
  }
  result.image = {.rgba = std::move(decoded->pixels), .width = decoded->width, .height = decoded->height};
  return result;
}

std::expected<std::vector<std::uint8_t>, std::string> luciole::writeThumbnailPng(const ThumbnailPng& thumbnail) {
  const auto& image = thumbnail.image;
  if (image.width < 1
      || image.height < 1
      || image.width > 1024
      || image.height > 1024
      || image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
    return std::unexpected("invalid thumbnail pixels");
  }
  png_image output{};
  output.version = PNG_IMAGE_VERSION;
  output.width = image.width;
  output.height = image.height;
  output.format = PNG_FORMAT_RGBA;
  // The documented bound avoids compressing the complete image once just to measure its size.
  png_alloc_size_t size = PNG_IMAGE_PNG_SIZE_MAX(output);
  std::vector<std::uint8_t> encoded(size);
  if (!png_image_write_to_memory(&output, encoded.data(), &size, 0, image.rgba.data(), 0, nullptr)) {
    png_image_free(&output);
    return std::unexpected("cannot encode thumbnail");
  }
  png_image_free(&output);
  encoded.resize(size);
  // Insert required standard tEXt keys directly before IEND, retaining ordinary RGBA PNG output.
  std::vector<std::uint8_t> metadata;
  const auto append32 = [&](std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      metadata.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  };
  for (const auto& [key, value] : thumbnail.text) {
    if (key.empty() || key.size() > 79 || value.size() > 131072 || key.contains('\0') || value.contains('\0')) {
      continue;
    }
    append32(static_cast<std::uint32_t>(key.size() + 1 + value.size()));
    const auto start = metadata.size();
    for (char c : std::string_view("tEXt")) {
      metadata.push_back(static_cast<std::uint8_t>(c));
    }
    metadata.insert(metadata.end(), key.begin(), key.end());
    metadata.push_back(0);
    metadata.insert(metadata.end(), value.begin(), value.end());
    // PNG uses the standard CRC-32 polynomial; no zlib API is needed for this small metadata block.
    append32(pngCrc(std::span<const std::uint8_t>(metadata).subspan(start)));
  }
  encoded.insert(encoded.end() - 12, metadata.begin(), metadata.end());
  return encoded;
}
