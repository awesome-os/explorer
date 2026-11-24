#include "imaging/png_decoder.h"

#include <png.h>

std::expected<DecodedRasterImage, std::string>
luciole::decodePng(std::span<const std::uint8_t> bytes, std::uint64_t maxPixels, int maxSide) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  if (bytes.empty() || !maxPixels || maxPixels > kMaxImagePixelBudget || maxSide < 1 || maxSide > kMaxImageSide) {
    return std::unexpected("invalid PNG input or pixel limit");
  }
  if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
    const std::string error(image.message);
    png_image_free(&image);
    return std::unexpected(error.empty() ? "invalid PNG image" : error);
  }
  if (!image.width
      || !image.height
      || image.width > static_cast<unsigned>(maxSide)
      || image.height > static_cast<unsigned>(maxSide)
      || static_cast<std::uint64_t>(image.width) * image.height > maxPixels) {
    png_image_free(&image);
    return std::unexpected("PNG dimensions exceed the decoded image budget");
  }
  image.format = PNG_FORMAT_RGBA;
  DecodedRasterImage decoded{
      .pixels = std::vector<std::uint8_t>(PNG_IMAGE_SIZE(image)),
      .width = static_cast<int>(image.width),
      .height = static_cast<int>(image.height)
  };
  if (png_image_finish_read(&image, nullptr, decoded.pixels.data(), 0, nullptr) == 0) {
    const std::string error(image.message);
    png_image_free(&image);
    return std::unexpected(error);
  }
  png_image_free(&image);
  return decoded;
}
