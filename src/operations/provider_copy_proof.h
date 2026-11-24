#pragma once

#include "fs/cancel_token.h"
#include "fs/gio_interop.h"
#include "fs/location.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace luciole::provider_history {
  struct Budget {
    std::size_t entries = 16384;
    std::uint64_t bytes = 256 * 1024 * 1024;
  };
  struct Tree {
    std::map<std::string, std::string> attributes;
    std::string digest;
    std::string name;
    std::vector<Tree> children;
    bool directory = false;
    bool operator==(const Tree&) const = default;
  };
  void check(const CancelToken& token);
  GioPtr<GFileInfo> info(const Location& location, const CancelToken& token, bool missing = false);
  std::vector<std::string> children(const Location& location, const CancelToken& token, std::size_t limit);
  Tree capture(const Location& location, Budget& budget, const CancelToken& token, unsigned depth = 0);
  void verify(const Location& location, const Tree& tree, const CancelToken& token);
  void remove(const Location& location, const Tree& tree, const CancelToken& token);
  void erase(const Location& location, const CancelToken& token);
  void move(const Location& from, const Location& to, const CancelToken& token);
} // namespace luciole::provider_history
