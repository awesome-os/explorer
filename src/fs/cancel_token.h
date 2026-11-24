#pragma once

#include <memory>

namespace luciole {
  struct CancelTokenAccess;

  // Shared cancellation flag for one job. Copies observe the same state; a new
  // job takes a fresh token so workers never read a member the owner replaces.
  class CancelToken {
  public:
    CancelToken();
    void cancel() const;
    bool cancelled() const;

  private:
    friend struct CancelTokenAccess;
    struct State;
    std::shared_ptr<State> m_state;
  };
} // namespace luciole
