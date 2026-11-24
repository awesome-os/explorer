#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace luciole {
  enum class StartupMode : std::uint8_t {
    Normal,
    // Started by D-Bus for an org.freedesktop.FileManager1 call; opens no initial window.
    FileManagerActivation,
    // Started by D-Bus as the FileChooser portal backend; opens only chooser windows and owns no other name.
    FileChooserService,
  };
  int runApplication(
      const std::optional<std::string>& initialLocation = std::nullopt, StartupMode mode = StartupMode::Normal
  );
} // namespace luciole
