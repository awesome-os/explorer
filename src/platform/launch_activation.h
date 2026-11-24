#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

struct wl_seat;
struct wl_surface;
struct xdg_activation_v1;
struct xdg_activation_token_v1;

namespace luciole {
  // Owns an independent activation binding and bounded asynchronous launch requests.
  class LaunchActivation {
  public:
    explicit LaunchActivation(xdg_activation_v1* manager);
    ~LaunchActivation();
    LaunchActivation(const LaunchActivation&) = delete;
    LaunchActivation& operator=(const LaunchActivation&) = delete;
    void request(
        wl_surface* surface, wl_seat* seat, std::uint32_t serial, const std::string& appId,
        std::function<void(std::string)> completed
    );
    // Asks the compositor to focus `surface` on behalf of the launch that issued `token`.
    void activate(const std::string& token, wl_surface* surface);

  private:
    struct Pending;
    void finish(xdg_activation_token_v1* token, std::string value);
    xdg_activation_v1* m_manager;
    bool m_closing = false;
    std::map<xdg_activation_token_v1*, std::unique_ptr<Pending>> m_pending;
  };
} // namespace luciole
