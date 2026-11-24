#include "fs/gio_ptr.h"
#include "thumbnails/metadata_probe.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>

namespace {
  using Json = nlohmann::json;
  using luciole::MetadataFields;
  std::string literal(std::string_view value) {
    const luciole::GString valid(
        g_utf8_make_valid(value.data(), static_cast<gssize>(std::min<std::size_t>(value.size(), 2048)))
    );
    std::string result;
    unsigned count = 0;
    for (const char* p = valid.get(); p && *p && count < 256; p = g_utf8_next_char(p), ++count) {
      const auto c = g_utf8_get_char(p);
      if (g_unichar_iscntrl(c)) {
        result += ' ';
      } else {
        result.append(p, static_cast<std::size_t>(g_utf8_next_char(p) - p));
      }
    }
    while (!result.empty() && result.back() == ' ') {
      result.pop_back();
    }
    return result;
  }
  std::string text(const Json& object, const char* key) {
    if (!object.is_object()) {
      return {};
    }
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? literal(found->get_ref<const std::string&>()) : std::string{};
  }
  std::optional<double> number(const Json& object, const char* key) {
    if (!object.is_object()) {
      return {};
    }
    const auto found = object.find(key);
    if (found == object.end()) {
      return {};
    }
    double result = 0;
    if (found->is_number()) {
      result = found->get<double>();
    } else if (found->is_string()) {
      const auto& value = found->get_ref<const std::string&>();
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return {};
      }
    } else {
      return {};
    }
    return std::isfinite(result) && result >= 0 && result <= 1.0e12 ? std::optional(result) : std::nullopt;
  }
  void add(MetadataFields& fields, std::string label, std::string_view value, bool preview = true) {
    if (!value.empty() && value != "N/A" && fields.size() < 24) {
      fields.push_back({std::move(label), literal(value), preview});
    }
  }
  std::string duration(double seconds) {
    const auto ms = static_cast<std::uint64_t>(std::round(seconds * 1000));
    auto value = std::format("{}:{:02}:{:02}", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60);
    if (ms % 1000) {
      value += std::format(".{:03}", ms % 1000);
    }
    return value;
  }
  std::string bitrate(double bits) {
    if (bits >= 1'000'000) {
      return std::format("{:.1f} Mbit/s", bits / 1'000'000);
    }
    return std::format("{:.1f} kbit/s", bits / 1000);
  }
  void video(MetadataFields& fields, const Json& stream) {
    add(fields, "Video codec", text(stream, "codec_name"));
    const auto width = number(stream, "width"), height = number(stream, "height");
    if (width && height && *width > 0 && *height > 0) {
      add(fields, "Frame size", std::format("{:.0f} × {:.0f} px", *width, *height));
    }
    const auto rate = text(stream, "avg_frame_rate");
    const auto slash = rate.find('/');
    if (slash != std::string::npos) {
      const auto numerator = number(Json{{"rate", rate.substr(0, slash)}}, "rate");
      const auto denominator = number(Json{{"rate", rate.substr(slash + 1)}}, "rate");
      if (numerator && denominator && *numerator > 0 && *denominator > 0) {
        const double framesPerSecond = *numerator / *denominator;
        if (std::isfinite(framesPerSecond) && framesPerSecond <= 1'000'000) {
          add(fields, "Frame rate", std::format("{:.3f} fps", framesPerSecond));
        }
      }
    }
    add(fields, "Pixel format", text(stream, "pix_fmt"), false);
    const auto space = text(stream, "color_space");
    if (space != "unknown" && space != "unspecified") {
      add(fields, "Color space", space, false);
    }
    if (const auto frames = number(stream, "nb_frames"); frames && *frames > 0) {
      add(fields, "Frames", std::format("{:.0f}", *frames), false);
    }
    const auto side = stream.find("side_data_list");
    if (side != stream.end() && side->is_array()) {
      for (const auto& item : *side) {
        // Rotation can be negative; unlike duration it is a signed display hint.
        const auto angle = item.find("rotation");
        if (angle != item.end() && angle->is_number()) {
          const auto rotation = angle->get<double>();
          if (std::isfinite(rotation) && std::abs(rotation) <= 3600 && rotation != 0) {
            add(fields, "Rotation", std::format("{:.0f}°", rotation));
            break;
          }
        }
      }
    }
  }
  void audio(MetadataFields& fields, const Json& stream) {
    add(fields, "Audio codec", text(stream, "codec_name"));
    if (const auto rate = number(stream, "sample_rate"); rate && *rate > 0) {
      add(fields, "Sample rate", std::format("{:.0f} Hz", *rate));
    }
    auto channels = text(stream, "channel_layout");
    if (channels.empty()) {
      if (const auto count = number(stream, "channels"); count && *count > 0) {
        channels = std::format("{:.0f}", *count);
      }
    }
    add(fields, "Channels", channels);
    if (const auto bits = number(stream, "bits_per_sample"); bits && *bits > 0) {
      add(fields, "Audio depth", std::format("{:.0f}-bit", *bits), false);
    }
    if (const auto bits = number(stream, "bit_rate"); bits && *bits > 0) {
      add(fields, "Audio bitrate", bitrate(*bits), false);
    }
  }
} // namespace

luciole::MetadataFields luciole::parseMediaMetadata(std::string_view data) {
  if (data.empty() || data.size() > 65536) {
    return {};
  }
  const auto root = Json::parse(data, nullptr, false);
  if (!root.is_object()) {
    return {};
  }
  MetadataFields fields;
  const auto format = root.find("format");
  std::optional<double> length;
  if (format != root.end() && format->is_object()) {
    add(fields, "Container", text(*format, "format_long_name"));
    length = number(*format, "duration");
    if (length) {
      add(fields, "Duration", duration(*length));
    }
    if (const auto bits = number(*format, "bit_rate"); bits && *bits > 0) {
      add(fields, "Bitrate", bitrate(*bits), false);
    }
  }
  bool seenVideo = false, seenAudio = false;
  const auto streams = root.find("streams");
  if (streams != root.end() && streams->is_array() && streams->size() <= 32) {
    for (const auto& stream : *streams) {
      if (!stream.is_object()) {
        continue;
      }
      const auto disposition = stream.find("disposition");
      const bool cover = disposition != stream.end() && number(*disposition, "attached_pic").value_or(0) != 0;
      const auto kind = text(stream, "codec_type");
      if (kind == "video" && !cover && !std::exchange(seenVideo, true)) {
        video(fields, stream);
      }
      if (kind == "audio" && !std::exchange(seenAudio, true)) {
        audio(fields, stream);
      }
      if (!length && (kind == "audio" || (kind == "video" && !cover))) {
        length = number(stream, "duration");
        if (length) {
          add(fields, "Duration", duration(*length));
        }
      }
    }
  }
  if (format != root.end()) {
    const auto tags = format->find("tags");
    if (tags != format->end() && tags->is_object()) {
      for (const auto& [name, value] : tags->items()) {
        const luciole::GString lower(g_ascii_strdown(name.c_str(), -1));
        const std::string_view key(lower.get());
        const char* label = key == "title" ? "Title"
            : key == "artist"              ? "Artist"
            : key == "album"               ? "Album"
            : key == "track"               ? "Track"
            : key == "date"                ? "Date"
                                           : nullptr;
        if (label && value.is_string()) {
          add(fields, label, value.get_ref<const std::string&>(), false);
        }
      }
    }
  }
  return fields;
}

luciole::MetadataFields luciole::parsePdfMetadata(std::string_view data) {
  if (data.size() > 65536) {
    return {};
  }
  std::map<std::string, std::string> values;
  while (!data.empty()) {
    const auto end = data.find('\n');
    const auto line = data.substr(0, end);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      auto value = line.substr(colon + 1);
      while (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
      }
      values[std::string(line.substr(0, colon))] = literal(value);
    }
    if (end == std::string_view::npos) {
      break;
    }
    data.remove_prefix(end + 1);
  }
  MetadataFields fields;
  for (const auto* key : {"Pages", "Page size", "PDF version", "Encrypted", "Title", "Author"}) {
    const auto found = values.find(key);
    if (found != values.end()) {
      add(fields, key, found->second, std::string_view(key) == "Pages" || std::string_view(key) == "Page size");
    }
  }
  return fields;
}
