#include "fs/folder_icon.h"

#include "fs/custom_icons.h"
#include "fs/gio_interop.h"

#include <cerrno>
#include <memory>
#include <string>
#include <unistd.h>

std::optional<std::string> luciole::readFolderCustomIcon(const Location& folder) {
  const auto path = folder.nativePath();
  if (!path) {
    return std::nullopt;
  }
  auto icon = customFolderIcon(*path);
  if (!icon) {
    return std::nullopt;
  }
  return icon->filePath.empty() ? std::move(icon->names.front()) : std::move(icon->filePath);
}

namespace {
  bool clearMetadataKey(GFile* file, const char* attribute, std::string& error) {
    GError* rawError = nullptr;
    const bool cleared = g_file_set_attribute(
                             file, attribute, G_FILE_ATTRIBUTE_TYPE_INVALID, nullptr,
                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &rawError
                         )
        != FALSE;
    const luciole::GErrorPtr metadataError(rawError);
    if (!cleared && metadataError) {
      error = metadataError->message;
      return false;
    }
    return true;
  }

  // Best-effort removal of a stale .directory Icon so a metadata write cannot
  // be shadowed by it later (and vice versa on reset).
  bool clearDirectoryFileIcon(const std::string& folderPath) {
    const std::string path = folderPath + "/.directory";
    const std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), &g_key_file_unref);
    GError* rawError = nullptr;
    if (!g_key_file_load_from_file(file.get(), path.c_str(), G_KEY_FILE_KEEP_TRANSLATIONS, &rawError)) {
      g_clear_error(&rawError);
      return true; // Nothing stored there.
    }
    GError* removeError = nullptr;
    if (!g_key_file_remove_key(file.get(), "Desktop Entry", "Icon", &removeError)) {
      g_clear_error(&removeError);
      return true; // No Icon key stored.
    }
    gsize groups = 0;
    const std::unique_ptr<gchar*, decltype(&g_strfreev)> names(g_key_file_get_groups(file.get(), &groups), &g_strfreev);
    gsize keys = 0;
    for (gsize i = 0; i < groups; ++i) {
      gsize count = 0;
      const std::unique_ptr<gchar*, decltype(&g_strfreev)> keyNames(
          g_key_file_get_keys(file.get(), names.get()[i], &count, nullptr), &g_strfreev
      );
      keys += count;
    }
    if (keys == 0) {
      // Nothing left: remove the file instead of leaving an empty group behind.
      return ::unlink(path.c_str()) == 0 || errno == ENOENT;
    }
    gsize length = 0;
    GError* dataError = nullptr;
    const std::unique_ptr<gchar, decltype(&g_free)> data(g_key_file_to_data(file.get(), &length, &dataError), &g_free);
    if (!data) {
      g_clear_error(&dataError);
      return false;
    }
    if (length == 0) {
      ::unlink(path.c_str());
      return true;
    }
    GError* saveError = nullptr;
    const bool saved = g_key_file_save_to_file(file.get(), path.c_str(), &saveError);
    g_clear_error(&saveError);
    return saved;
  }

  bool writeDirectoryFileIcon(const std::string& folderPath, const std::string& iconName, std::string& error) {
    const std::string path = folderPath + "/.directory";
    const std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), &g_key_file_unref);
    GError* loadError = nullptr;
    (void)g_key_file_load_from_file(file.get(), path.c_str(), G_KEY_FILE_KEEP_TRANSLATIONS, &loadError);
    g_clear_error(&loadError);
    g_key_file_set_string(file.get(), "Desktop Entry", "Icon", iconName.c_str());
    GError* saveError = nullptr;
    const bool saved = g_key_file_save_to_file(file.get(), path.c_str(), &saveError);
    if (!saved) {
      error = saveError != nullptr ? saveError->message : "Could not write the folder's .directory file.";
    }
    g_clear_error(&saveError);
    return saved;
  }
} // namespace

bool luciole::setFolderCustomIcon(const Location& folder, const std::string& iconName, std::string& error) {
  if (iconName.empty()) {
    return clearFolderCustomIcon(folder, error);
  }
  const auto path = folder.nativePath();
  // Local folders always get a .directory entry: the native fast listing path
  // used to carry no GFileInfo, so metadata alone would never display there.
  // Non-local folders rely on metadata only.
  std::string directoryError;
  const bool directoryOk = !path || writeDirectoryFileIcon(*path, iconName, directoryError);
  const GioPtr<GFile> file(gioFile(folder));
  // A theme name belongs in custom-icon-name; an image file in custom-icon as
  // a file:// URI. The other key is cleared so GNOME Files never sees stale
  // data (including bare names this app used to store in custom-icon).
  const bool image = iconName.starts_with('/') || iconName.starts_with("file://");
  std::string imageUri;
  if (iconName.starts_with('/')) {
    GError* rawError = nullptr;
    const GString uri(g_filename_to_uri(iconName.c_str(), nullptr, &rawError));
    GErrorPtr uriError(rawError);
    if (!uri) {
      error = uriError ? uriError->message : "Could not address the image file.";
      return false;
    }
    imageUri = uri.get();
  } else if (iconName.starts_with("file://")) {
    imageUri = iconName;
  }
  std::string metadataError;
  bool stored = false;
  if (image) {
    GError* rawError = nullptr;
    stored = g_file_set_attribute_string(
                 file.get(), kFolderCustomIconAttribute, imageUri.c_str(), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr,
                 &rawError
             )
        != FALSE;
    GErrorPtr storeError(rawError);
    if (stored) {
      stored = clearMetadataKey(file.get(), kFolderCustomIconNameAttribute, metadataError);
    } else if (storeError) {
      metadataError = storeError->message;
    }
  } else {
    GError* rawError = nullptr;
    stored = g_file_set_attribute_string(
                 file.get(), kFolderCustomIconNameAttribute, iconName.c_str(), G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                 nullptr, &rawError
             )
        != FALSE;
    GErrorPtr storeError(rawError);
    if (stored) {
      // Also clears bare theme names older versions stored in custom-icon.
      stored = clearMetadataKey(file.get(), kFolderCustomIconAttribute, metadataError);
    } else if (storeError) {
      metadataError = storeError->message;
    }
  }
  if (path) {
    if (!directoryOk) {
      error = directoryError;
      if (!metadataError.empty()) {
        error += " (" + metadataError + ")";
      }
      return false;
    }
    // A metadata failure here only loses GNOME Files interop, not the icon itself.
    return true;
  }
  if (stored) {
    return true;
  }
  if (!metadataError.empty()) {
    error = metadataError;
  } else {
    error = "Setting a folder icon is not supported here.";
  }
  return false;
}

bool luciole::clearFolderCustomIcon(const Location& folder, std::string& error) {
  const GioPtr<GFile> file(gioFile(folder));
  std::string firstError;
  const bool uriCleared = clearMetadataKey(file.get(), kFolderCustomIconAttribute, firstError);
  std::string nameError;
  const bool nameCleared = clearMetadataKey(file.get(), kFolderCustomIconNameAttribute, nameError);
  bool directoryOk = true;
  if (const auto path = folder.nativePath()) {
    directoryOk = clearDirectoryFileIcon(*path);
  }
  if (!uriCleared || !nameCleared) {
    // Metadata actively refused and no local fallback: report it.
    if (!directoryOk || !folder.nativePath()) {
      error = !firstError.empty() ? firstError : nameError;
      return false;
    }
  }
  return directoryOk;
}
