#include "operations/archive_icons.h"

#include "fs/gio_interop.h"
#include "fs/gio_ptr.h"

#include <gio/gio.h>

void luciole::assignArchiveIcons(ArchiveCatalog& catalog) {
  for (auto& entry : catalog.entries) {
    if (entry.kind == ArchiveEntryKind::Directory) {
      entry.icon.names = {"folder"};
      continue;
    }
    const GString mime(g_content_type_guess(entry.name.c_str(), nullptr, 0, nullptr));
    const GioPtr<GIcon> icon(mime ? g_content_type_get_icon(mime.get()) : nullptr);
    entry.icon = readIconSource(icon.get());
    if (entry.icon.names.empty()) {
      entry.icon.names = {"text-x-generic"};
    }
  }
}
