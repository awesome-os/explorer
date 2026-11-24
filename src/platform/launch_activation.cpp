#include "platform/launch_activation.h"

#include "core/timer_manager.h"
#include "xdg-activation-v1-client-protocol.h"

#include <chrono>
#include <utility>

struct luciole::LaunchActivation::Pending {
  LaunchActivation& owner;
  std::function<void(std::string)> completed;
  Timer timeout;
};

luciole::LaunchActivation::LaunchActivation(xdg_activation_v1* manager) : m_manager(manager) {}
luciole::LaunchActivation::~LaunchActivation() {
  m_closing = true;
  while (!m_pending.empty()) {
    finish(m_pending.begin()->first, {});
  }
  if (m_manager) {
    xdg_activation_v1_destroy(m_manager);
  }
}
void luciole::LaunchActivation::request(
    wl_surface* surface, wl_seat* seat, std::uint32_t serial, const std::string& appId,
    std::function<void(std::string)> completed
) {
  if (m_closing || !m_manager || !surface || !seat || !serial || m_pending.size() >= 32) {
    completed({});
    return;
  }
  auto* token = xdg_activation_v1_get_activation_token(m_manager);
  auto pending = std::make_unique<Pending>(*this, std::move(completed));
  static const xdg_activation_token_v1_listener listener{
      .done = [](void* data, xdg_activation_token_v1* token, const char* value) {
        auto& owner = static_cast<Pending*>(data)->owner;
        owner.finish(token, value ? value : "");
      }
  };
  xdg_activation_token_v1_add_listener(token, &listener, pending.get());
  xdg_activation_token_v1_set_serial(token, serial, seat);
  xdg_activation_token_v1_set_surface(token, surface);
  if (!appId.empty()) {
    const auto id = appId.ends_with(".desktop") ? appId.substr(0, appId.size() - 8) : appId;
    xdg_activation_token_v1_set_app_id(token, id.c_str());
  }
  pending->timeout.start(std::chrono::milliseconds(250), [this, token] { finish(token, {}); });
  m_pending.emplace(token, std::move(pending));
  xdg_activation_token_v1_commit(token);
}
void luciole::LaunchActivation::activate(const std::string& token, wl_surface* surface) {
  if (m_manager && surface && !token.empty()) {
    xdg_activation_v1_activate(m_manager, token.c_str(), surface);
  }
}
void luciole::LaunchActivation::finish(xdg_activation_token_v1* token, std::string value) {
  const auto found = m_pending.find(token);
  if (found == m_pending.end()) {
    return;
  }
  auto completed = std::move(found->second->completed);
  xdg_activation_token_v1_destroy(token);
  m_pending.erase(found);
  completed(std::move(value));
}
