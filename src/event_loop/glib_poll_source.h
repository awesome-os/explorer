#pragma once

#include "core/poll_source.h"

#include <glib.h>

// Adapted from Noctalia's WirePlumber poll integration, without audio services.
// The application owns the default GLib context on this thread.
class GlibPollSource final : public PollSource {
public:
  GlibPollSource();
  ~GlibPollSource() override;
  GlibPollSource(const GlibPollSource&) = delete;
  GlibPollSource& operator=(const GlibPollSource&) = delete;

  int pollTimeoutMs() const override { return m_timeout; }
  void dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) override;

protected:
  void doAddPollFds(std::vector<pollfd>& fds) override;

private:
  GMainContext* m_context;
  std::vector<GPollFD> m_fds;
  int m_priority = G_PRIORITY_DEFAULT;
  int m_timeout = -1;
};
