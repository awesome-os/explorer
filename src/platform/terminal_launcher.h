#pragma once

#include "fs/location.h"

#include <functional>
#include <memory>

namespace luciole {
  class TerminalLauncher {
  public:
    TerminalLauncher();
    ~TerminalLauncher();
    TerminalLauncher(const TerminalLauncher&) = delete;
    TerminalLauncher& operator=(const TerminalLauncher&) = delete;
    void setCommand(std::string command);
    bool available() const;
    void open(const Location& folder, std::function<void(std::string)> completed);

  private:
    struct State;
    std::shared_ptr<State> m_state;
  };
} // namespace luciole
