#pragma once

#include "ui/palette.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace luciole {
  // Built-in dark palette, used for every role colors.toml does not set.
  Palette defaultPalette();
  // $XDG_CONFIG_HOME/luciole/colors.toml
  std::filesystem::path palettePath();
  // Reads `[colors]` with keys named like Noctalia's template tokens (`primary`, `surface_container`, ...)
  // and "#rrggbb" or "#rrggbbaa" values. Unknown keys are ignored; roles left out keep `base`.
  std::expected<Palette, std::string> parsePalette(std::string_view text, std::string_view source, const Palette& base);
  // A missing file yields `base`.
  std::expected<Palette, std::string> loadPalette(const std::filesystem::path& path, const Palette& base);
} // namespace luciole
