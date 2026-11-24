#pragma once

#include "fs/gio_ptr.h"

#include <gio/gio.h>
#include <string>

namespace luciole {
  GioPtr<GAppLaunchContext> launchContext(const std::string& activationToken);
} // namespace luciole
