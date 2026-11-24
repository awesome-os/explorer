#include "fs/folder_icon.h"
#include "fs/gio_interop.h"
#include "system/icon_resolver.h"
#include "test_check.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>

namespace {
  namespace fs = std::filesystem;
  using luciole::GioPtr;

  std::optional<std::string> rawMetadata(const luciole::Location& location, const char* attribute) {
    GError* rawError = nullptr;
    const GioPtr<GFileInfo> info(
        g_file_query_info(gioFile(location).get(), attribute, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &rawError)
    );
    g_clear_error(&rawError);
    if (!info) {
      return std::nullopt;
    }
    if (const char* value = g_file_info_get_attribute_string(info.get(), attribute)) {
      return std::string(value);
    }
    return std::nullopt;
  }

  // The metadata daemon is required for key-placement assertions; .directory assertions run with or without it.
  bool metadataAvailable(const luciole::Location& location) {
    GError* rawError = nullptr;
    const bool stored = g_file_set_attribute_string(
                            gioFile(location).get(), luciole::kFolderCustomIconNameAttribute, "luciole-probe",
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &rawError
                        )
        != FALSE;
    g_clear_error(&rawError);
    if (!stored) {
      return false;
    }
    const bool found = rawMetadata(location, luciole::kFolderCustomIconNameAttribute) == "luciole-probe";
    g_file_set_attribute(
        gioFile(location).get(), luciole::kFolderCustomIconNameAttribute, G_FILE_ATTRIBUTE_TYPE_INVALID, nullptr,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr
    );
    return found;
  }

  void roundtrip(const fs::path& root) {
    const auto folder = root / "styled";
    fs::create_directory(folder);
    const auto location = luciole::Location::fromPath(folder.native());
    TEST_CHECK(!luciole::readFolderCustomIcon(location).has_value());

    // A theme name lands in the name key plus the .directory file.
    std::string error;
    TEST_CHECK(luciole::setFolderCustomIcon(location, "folder-red", error));
    TEST_CHECK(luciole::readFolderCustomIcon(location) == "folder-red");
    {
      std::ifstream stream(folder / ".directory");
      const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      TEST_CHECK(text.contains("Icon=folder-red"));
    }
    if (metadataAvailable(location)) {
      TEST_CHECK(rawMetadata(location, luciole::kFolderCustomIconNameAttribute) == "folder-red");
      TEST_CHECK(!rawMetadata(location, luciole::kFolderCustomIconAttribute).has_value());
    }

    // An image path becomes a file:// URI in the URI key; the name key stays clear.
    const fs::path image = root / "art.png";
    std::ofstream(image) << "x";
    TEST_CHECK(luciole::setFolderCustomIcon(location, image.native(), error));
    TEST_CHECK(luciole::readFolderCustomIcon(location) == image.native());
    if (metadataAvailable(location)) {
      const auto uri = rawMetadata(location, luciole::kFolderCustomIconAttribute);
      TEST_CHECK(uri && uri->starts_with("file://") && uri->ends_with("art.png"));
      TEST_CHECK(!rawMetadata(location, luciole::kFolderCustomIconNameAttribute).has_value());
    }

    TEST_CHECK(luciole::clearFolderCustomIcon(location, error));
    TEST_CHECK(!luciole::readFolderCustomIcon(location).has_value());
    TEST_CHECK(!fs::exists(folder / ".directory"));
    if (metadataAvailable(location)) {
      TEST_CHECK(!rawMetadata(location, luciole::kFolderCustomIconAttribute).has_value());
      TEST_CHECK(!rawMetadata(location, luciole::kFolderCustomIconNameAttribute).has_value());
    }
  }

  void clearKeepsOtherKeys(const fs::path& root) {
    const auto folder = root / "dolphin";
    fs::create_directory(folder);
    const auto location = luciole::Location::fromPath(folder.native());

    // Clearing removes a hand-written Icon= and deletes the file when nothing else remains.
    std::ofstream(folder / ".directory") << "[Desktop Entry]\nIcon=folder-blue\n";
    TEST_CHECK(luciole::readFolderCustomIcon(location) == "folder-blue");
    std::string error;
    TEST_CHECK(luciole::clearFolderCustomIcon(location, error));
    TEST_CHECK(!luciole::readFolderCustomIcon(location).has_value());
    TEST_CHECK(!fs::exists(folder / ".directory"));

    // Other Dolphin settings survive the reset.
    std::ofstream(folder / ".directory") << "[Desktop Entry]\nIcon=folder-blue\n\n[Dolphin]\nViewMode=1\n";
    TEST_CHECK(luciole::clearFolderCustomIcon(location, error));
    TEST_CHECK(!luciole::readFolderCustomIcon(location).has_value());
    std::ifstream stream(folder / ".directory");
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    TEST_CHECK(text.contains("ViewMode=1") && !text.contains("Icon="));
  }

  void themeListingIsSane() {
    const auto names = IconResolver::listThemeIcons();
    for (const auto& icon : names) {
      // Double extensions (foo.png.svg) are legitimate resolvable names.
      TEST_CHECK(!icon.name.empty() && !icon.name.contains('/'));
      TEST_CHECK(icon.context.empty() || (!icon.context.contains('/') && !icon.context.contains(' ')));
    }
    TEST_CHECK(std::ranges::is_sorted(names, {}, &ThemedIcon::name));
    TEST_CHECK(std::ranges::adjacent_find(names, {}, &ThemedIcon::name) == names.end());
  }
} // namespace

int main() {
  const luciole::GString temporary(g_dir_make_tmp("luciole-folder-icon-XXXXXX", nullptr));
  TEST_CHECK(temporary);
  const fs::path root(temporary.get());
  roundtrip(root);
  clearKeepsOtherKeys(root);
  themeListingIsSane();
  fs::remove_all(root);
  std::println("folder icon checks passed");
}
