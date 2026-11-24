#pragma once

#include "fs/location.h"

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <optional>
#include <string>
#include <vector>

namespace luciole {
  // One org.freedesktop.Application call: a window to open, with one tab per location.
  struct InstanceRequest {
    std::vector<Location> locations;
    // xdg-activation token of the launch that sent the request, empty when there was none.
    std::string activationToken;
  };

  // Owns dev.noctalia.luciole on the session bus so every launch opens its windows in one process.
  class InstanceService {
  public:
    enum class Role : std::uint8_t {
      // This process owns the name and receives later launches.
      Primary,
      // Another process owns the name; forward() hands this launch to it.
      Secondary,
      // No session bus or the name could not be claimed: this process runs on its own.
      Standalone,
    };
    using Handler = std::function<void(InstanceRequest)>;

    // Claims the name synchronously, before any window exists.
    InstanceService();
    ~InstanceService();
    InstanceService(const InstanceService&) = delete;
    InstanceService& operator=(const InstanceService&) = delete;

    Role role() const { return m_role; }
    // Secondary only: asks the primary process for a window at `location`, or at its default without one.
    bool forward(const std::optional<Location>& location, const std::string& activationToken);
    // Primary only: requests received before the handler is set are delivered here first.
    void setHandler(Handler handler);
    // Gives the name back once no window can show requests; later launches start their own primary.
    void release();

  private:
    static void methodCall(
        GDBusConnection* connection, const char* sender, const char* path, const char* interface, const char* method,
        GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data
    );
    void deliver(InstanceRequest request);
    void unregister();

    Handler m_handler;
    std::vector<InstanceRequest> m_pending;
    GDBusNodeInfo* m_node = nullptr;
    GDBusConnection* m_connection = nullptr;
    guint m_registration = 0;
    Role m_role = Role::Standalone;
  };
} // namespace luciole
