#include "fs/directory_watch.h"

#include "fs/gio_interop.h"

#include <utility>

struct luciole::DirectoryWatch::State {
  GioPtr<GFileMonitor> monitor;
  std::function<void(const std::filesystem::path&)> changed;
};

std::unique_ptr<luciole::DirectoryWatch> luciole::DirectoryWatch::start(
    const std::filesystem::path& directory, std::function<void(const std::filesystem::path&)> changed
) {
  const GioPtr<GFile> file(g_file_new_for_path(directory.c_str()));
  GioPtr<GFileMonitor> monitor(g_file_monitor_directory(file.get(), G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr));
  if (!monitor) {
    return nullptr;
  }
  auto state = std::make_unique<State>(std::move(monitor), std::move(changed));
  g_signal_connect(
      state->monitor.get(), "changed",
      G_CALLBACK(+[](GFileMonitor*, GFile* file, GFile* other, GFileMonitorEvent, gpointer data) {
        const auto& owner = *static_cast<State*>(data);
        for (auto* candidate : {file, other}) {
          if (!candidate) {
            continue;
          }
          if (const GString path(g_file_get_path(candidate)); path) {
            owner.changed(path.get());
          }
        }
      }),
      state.get()
  );
  return std::unique_ptr<DirectoryWatch>(new DirectoryWatch(std::move(state)));
}

luciole::DirectoryWatch::DirectoryWatch(std::unique_ptr<State> state) : m_state(std::move(state)) {}

luciole::DirectoryWatch::~DirectoryWatch() {
  g_signal_handlers_disconnect_by_data(m_state->monitor.get(), m_state.get());
  g_file_monitor_cancel(m_state->monitor.get());
}
