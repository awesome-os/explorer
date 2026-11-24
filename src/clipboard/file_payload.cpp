#include "clipboard/file_payload.h"

#include "fs/gio_interop.h"

#include <algorithm>
#include <unordered_set>

std::expected<luciole::FilePayload, std::string>
luciole::parseFilePayload(std::string_view mime, std::string_view bytes) {
  if ((mime != kUriListMime && mime != kGnomeFilesMime)
      || bytes.empty()
      || bytes.size() > kFilePayloadLimit
      || bytes.contains('\0')
      || !g_utf8_validate(bytes.data(), static_cast<gssize>(bytes.size()), nullptr)) {
    return std::unexpected("Invalid or oversized file clipboard data.");
  }
  FilePayload payload;
  bool header = mime == kGnomeFilesMime;
  std::unordered_set<std::string> seen;
  while (!bytes.empty()) {
    const auto end = bytes.find('\n');
    auto line = bytes.substr(0, end);
    bytes = end == std::string_view::npos ? std::string_view{} : bytes.substr(end + 1);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    if (header) {
      if (line != "copy" && line != "cut") {
        return std::unexpected("Invalid file clipboard action.");
      }
      payload.cut = line == "cut";
      header = false;
      continue;
    }
    if (mime == kUriListMime && (line.empty() || line.starts_with('#'))) {
      continue;
    }
    if (line.empty() || std::ranges::any_of(line, [](unsigned char ch) { return ch <= 0x20 || ch == 0x7f; })) {
      return std::unexpected("File URIs must escape spaces and control characters.");
    }
    try {
      const std::unique_ptr<GUri, decltype(&g_uri_unref)> uri(
          g_uri_parse(std::string(line).c_str(), G_URI_FLAGS_ENCODED, nullptr), &g_uri_unref
      );
      if (!uri || g_uri_get_fragment(uri.get()) || g_uri_get_query(uri.get()) || line.contains("%00")) {
        return std::unexpected("Invalid file URI in clipboard data.");
      }
      auto location = Location::fromUri(std::string(line));
      if (!seen.insert(location.uri()).second) {
        return std::unexpected("The file payload contains duplicate sources.");
      }
      payload.locations.push_back(std::move(location));
    } catch (const std::exception&) {
      return std::unexpected("Invalid file URI in clipboard data.");
    }
    if (payload.locations.size() > kFilePayloadCount) {
      return std::unexpected("Too many files in the clipboard data.");
    }
  }
  if (payload.locations.empty()) {
    return std::unexpected("The clipboard contains no file URIs.");
  }
  return payload;
}

std::expected<std::string, std::string> luciole::encodeFilePayload(const FilePayload& payload, std::string_view mime) {
  if (payload.locations.empty() || payload.locations.size() > kFilePayloadCount) {
    return std::unexpected("Select between one and 4096 items.");
  }
  if (mime == kKdeCutMime) {
    return payload.cut ? "1" : "0";
  }
  if (mime != kUriListMime && mime != kGnomeFilesMime) {
    return std::unexpected("Unsupported file clipboard format.");
  }
  std::string bytes = mime == kGnomeFilesMime ? (payload.cut ? "cut" : "copy") : "";
  for (const auto& location : payload.locations) {
    if (mime == kGnomeFilesMime) {
      bytes += '\n';
    }
    bytes += location.uri();
    if (mime == kUriListMime) {
      bytes += "\r\n";
    }
    if (bytes.size() > kFilePayloadLimit) {
      return std::unexpected("The file selection exceeds the clipboard limit.");
    }
  }
  if (!parseFilePayload(mime, bytes)) {
    return std::unexpected("The selection cannot be represented as file URIs.");
  }
  return bytes;
}

luciole::FilePayload luciole::remainingCutFiles(FilePayload payload, const std::vector<TransferResult>& results) {
  if (payload.cut) {
    std::unordered_set<std::string> moved;
    for (const auto& result : results) {
      if (result.outcome == TransferOutcome::Moved) {
        moved.insert(result.source.uri());
      }
    }
    std::erase_if(payload.locations, [&](const Location& location) { return moved.contains(location.uri()); });
  }
  return payload;
}
