#include "event_loop/glib_poll_source.h"

#include <stdexcept>

GlibPollSource::GlibPollSource() : m_context(g_main_context_ref(g_main_context_default())) {
  if (!g_main_context_acquire(m_context)) {
    g_main_context_unref(m_context);
    throw std::runtime_error("cannot acquire the default GLib context");
  }
  g_main_context_push_thread_default(m_context);
}

GlibPollSource::~GlibPollSource() {
  g_main_context_pop_thread_default(m_context);
  g_main_context_release(m_context);
  g_main_context_unref(m_context);
}

void GlibPollSource::doAddPollFds(std::vector<pollfd>& fds) {
  const bool ready = g_main_context_prepare(m_context, &m_priority) != FALSE;
  int count = g_main_context_query(m_context, m_priority, &m_timeout, nullptr, 0);
  // A worker can attach a source between queries; grow until all fds fit.
  while (true) {
    m_fds.resize(static_cast<std::size_t>(count));
    const int needed = g_main_context_query(m_context, m_priority, &m_timeout, m_fds.data(), count);
    if (needed <= count) {
      m_fds.resize(static_cast<std::size_t>(needed));
      break;
    }
    count = needed;
  }
  if (ready) {
    m_timeout = 0;
  }
  for (const auto& fd : m_fds) {
    fds.push_back({.fd = fd.fd, .events = static_cast<short>(fd.events), .revents = 0});
  }
}

void GlibPollSource::dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
  for (std::size_t i = 0; i < m_fds.size(); ++i) {
    m_fds[i].revents = static_cast<gushort>(fds.at(startIdx + i).revents);
  }
  if (g_main_context_check(m_context, m_priority, m_fds.data(), static_cast<int>(m_fds.size()))) {
    // One dispatch pass per application-loop iteration preserves input turns.
    g_main_context_dispatch(m_context);
  }
}
