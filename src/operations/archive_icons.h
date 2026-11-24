#pragma once

#include "operations/archive_service.h"

namespace luciole {
  // Fills each entry's icon from its name's guessed content type; folders use "folder".
  void assignArchiveIcons(ArchiveCatalog& catalog);
} // namespace luciole
