#include "fs/gio_ptr.h"
#include "imaging/image_limits.h"
#include "imaging/svg_rasterizer.h"
#include "render/core/image_orientation.h"
#include "thumbnails/preview_metadata.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  luciole::MetadataFields imageInfo(std::span<const std::uint8_t> bytes, std::string_view mime) {
    int width = 0, height = 0;
    std::string format;
    if (mime == "image/svg+xml" || mime == "image/svg+xml-compressed") {
      auto image = luciole::rasterizeSvg(
          bytes, 1, false, static_cast<std::uint64_t>(luciole::kMaxSvgSourceSide * luciole::kMaxSvgSourceSide)
      );
      if (!image) {
        return {};
      }
      width = image->sourceWidth;
      height = image->sourceHeight;
      format = "SVG";
    } else {
      auto* info = gdk_pixbuf_get_file_info("/proc/self/fd/3", &width, &height);
      if (!info || width <= 0 || height <= 0) {
        return {};
      }
      const luciole::GString name(gdk_pixbuf_format_get_name(info));
      if (!name || std::string_view(name.get()) == "svg") {
        return {};
      }
      const luciole::GString upper(g_ascii_strup(name.get(), -1));
      format = upper.get();
    }
    luciole::MetadataFields fields{{"Format", format}, {"Pixel size", std::format("{} × {} px", width, height)}};
    const auto orientation = exifOrientation(bytes.data(), bytes.size());
    constexpr std::array<std::string_view, 9> orientations{
        "",
        "",
        "Mirrored horizontally",
        "180°",
        "Mirrored vertically",
        "Transposed",
        "90° clockwise",
        "Transverse",
        "90° counterclockwise"
    };
    if (orientation != ImageOrientation::Normal) {
      fields.push_back({"Orientation", std::string(orientations[static_cast<std::size_t>(orientation)])});
    }
    constexpr std::array<std::uint8_t, 8> png{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= 33 && std::equal(png.begin(), png.end(), bytes.begin())) {
      const auto depth = bytes[24], color = bytes[25];
      fields.push_back({"Bit depth", std::format("{}-bit", depth), false});
      const char* model = color == 0 ? "Grayscale"
          : color == 2               ? "RGB"
          : color == 3               ? "Indexed"
          : color == 4               ? "Grayscale + alpha"
          : color == 6               ? "RGBA"
                                     : nullptr;
      if (model) {
        fields.push_back({"Color", model, false});
      }
    }
    if (mime == "image/svg+xml-compressed") {
      fields.push_back({"Compression", "Gzip", false});
    }
    return fields;
  }
} // namespace

int luciole::runPreviewMetadataWorker(std::span<const std::string_view> args) {
  if (args.size() != 2 || !args[0].starts_with("image/")) {
    return 2;
  }
  std::uint64_t maximum = 0;
  const auto parsed = std::from_chars(args[1].data(), args[1].data() + args[1].size(), maximum);
  if (parsed.ec != std::errc{}
      || parsed.ptr != args[1].data() + args[1].size()
      || maximum == 0
      || maximum > kMaxEncodedImageBytes) {
    return 2;
  }
  struct stat info{};
  if (::fstat(3, &info) < 0
      || !S_ISREG(info.st_mode)
      || info.st_size <= 0
      || static_cast<std::uint64_t>(info.st_size) > maximum) {
    return 2;
  }
  try {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto read = ::pread(3, bytes.data() + offset, bytes.size() - offset, static_cast<off_t>(offset));
      if (read <= 0) {
        return 1;
      }
      offset += static_cast<std::size_t>(read);
    }
    const auto fields = imageInfo(bytes, args[0]);
    if (fields.empty()) {
      return 1;
    }
    nlohmann::json output = nlohmann::json::array();
    for (const auto& field : fields) {
      output.push_back({{"label", field.label}, {"value", field.value}, {"preview", field.preview}});
    }
    const auto text = output.dump();
    offset = 0;
    while (offset < text.size()) {
      const auto written = ::write(STDOUT_FILENO, text.data() + offset, text.size() - offset);
      if (written <= 0) {
        return 1;
      }
      offset += static_cast<std::size_t>(written);
    }
    return 0;
  } catch (const std::exception&) {
    return 1;
  }
}
