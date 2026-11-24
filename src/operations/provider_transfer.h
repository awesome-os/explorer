#pragma once

#include "fs/cancel_token.h"
#include "operations/transfer_service.h"

#include <atomic>
#include <functional>
#include <memory>

namespace luciole {
  namespace transfer {
    struct Work;
  }
  class CopyReceipt;
  class MoveReceipt;
  // Worker-owned state for jobs involving a verified GIO remote provider.
  class ProviderTransfer {
  public:
    explicit ProviderTransfer(TransferRequest request);
    ~ProviderTransfer();
    using Choose = std::function<TransferConflictChoice(TransferConflict)>;
    // Local copies can reuse native publication hooks; optional provider history also covers eligible Moves.
    std::vector<TransferResult>
    run(const CancelToken& token, std::atomic<std::uint64_t>& bytes, std::atomic<std::size_t>& completed,
        const Choose& choose, transfer::Work* nativeCopies = nullptr, bool captureRemoteHistory = false);
    std::shared_ptr<const CopyReceipt> takeCopyReceipt(const CancelToken& token);
    std::shared_ptr<const MoveReceipt> takeMoveReceipt(const CancelToken& token);
    // Counts committed copy/link leaves, excluding merges/skips, only when the whole selection completed.
    std::optional<std::size_t> completedCopies() const;
    std::size_t retryable() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
  };
} // namespace luciole
