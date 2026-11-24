#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace luciole {
  // Main-context monitor for one local directory. changed receives each path the
  // provider reports, including both ends of a move, and must not throw.
  class DirectoryWatch {
  public:
    // nullptr when the directory cannot be monitored.
    static std::unique_ptr<DirectoryWatch>
    start(const std::filesystem::path& directory, std::function<void(const std::filesystem::path&)> changed);
    ~DirectoryWatch();
    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;

  private:
    struct State;
    explicit DirectoryWatch(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
  };
} // namespace luciole
