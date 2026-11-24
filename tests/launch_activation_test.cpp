#include "core/timer_manager.h"
#include "launch-activation-server-protocol.h"
#include "platform/launch_activation.h"
#include "platform/launch_context.h"
#include "test_check.h"

#include <algorithm>
#include <chrono>
#include <poll.h>
#include <print>
#include <string_view>
#include <sys/socket.h>
#include <utility>
#include <vector>
#include <wayland-client.h>

namespace {
  using namespace std::chrono_literals;
  // A private socket pair exercises delivery, timeout and teardown without a desktop.
  class Peer {
    wl_display* m_server = wl_display_create();
    wl_display* m_client = nullptr;
    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    wl_seat* m_seat = nullptr;
    wl_surface* m_surface = nullptr;
    xdg_activation_v1* m_manager = nullptr;
    wl_resource* m_source = nullptr;

  public:
    struct Request {
      wl_resource* token;
      std::uint32_t serial = 0;
      std::string appId;
      bool source = false, seat = false, committed = false;
    };
    std::vector<std::unique_ptr<Request>> requests;
    Peer() {
      TEST_CHECK(m_server);
      wl_global_create(
          m_server, &wl_seat_interface, 1, nullptr, [](wl_client* client, void*, std::uint32_t, std::uint32_t id) {
            auto* resource = wl_resource_create(client, &wl_seat_interface, 1, id);
            wl_resource_set_implementation(resource, nullptr, nullptr, nullptr);
          }
      );
      wl_global_create(
          m_server, &wl_compositor_interface, 1, this,
          [](wl_client* client, void* data, std::uint32_t, std::uint32_t id) {
            auto* resource = wl_resource_create(client, &wl_compositor_interface, 1, id);
            static const struct wl_compositor_interface impl{
                .create_surface =
                    [](wl_client* client, wl_resource* resource, std::uint32_t id) {
                      auto& peer = *static_cast<Peer*>(wl_resource_get_user_data(resource));
                      peer.m_source = wl_resource_create(client, &wl_surface_interface, 1, id);
                      static const auto surface = [] {
                        struct wl_surface_interface value{};
                        value.destroy = [](wl_client*, wl_resource* value) { wl_resource_destroy(value); };
                        return value;
                      }();
                      wl_resource_set_implementation(peer.m_source, &surface, nullptr, nullptr);
                    },
                .create_region = nullptr,
                .release = nullptr
            };
            wl_resource_set_implementation(resource, &impl, data, nullptr);
          }
      );
      wl_global_create(
          m_server, &xdg_activation_v1_interface, 1, this,
          [](wl_client* client, void* data, std::uint32_t, std::uint32_t id) {
            auto* resource = wl_resource_create(client, &xdg_activation_v1_interface, 1, id);
            static const struct xdg_activation_v1_interface impl{
                .destroy = [](wl_client*, wl_resource* value) { wl_resource_destroy(value); },
                .get_activation_token =
                    [](wl_client* client, wl_resource* manager, std::uint32_t id) {
                      auto& peer = *static_cast<Peer*>(wl_resource_get_user_data(manager));
                      auto* token = wl_resource_create(client, &xdg_activation_token_v1_interface, 1, id);
                      peer.requests.push_back(std::make_unique<Request>(token));
                      static const struct xdg_activation_token_v1_interface impl{
                          .set_serial =
                              [](wl_client*, wl_resource* resource, std::uint32_t serial, wl_resource* seat) {
                                auto& value = *static_cast<Request*>(wl_resource_get_user_data(resource));
                                value.serial = serial;
                                value.seat = seat != nullptr;
                              },
                          .set_app_id =
                              [](wl_client*, wl_resource* resource, const char* appId) {
                                static_cast<Request*>(wl_resource_get_user_data(resource))->appId = appId;
                              },
                          .set_surface =
                              [](wl_client*, wl_resource* resource, wl_resource* surface) {
                                static_cast<Request*>(wl_resource_get_user_data(resource))->source = surface != nullptr;
                              },
                          .commit = [](
                                        wl_client*, wl_resource* resource
                                    ) { static_cast<Request*>(wl_resource_get_user_data(resource))->committed = true; },
                          .destroy = [](wl_client*, wl_resource* value) { wl_resource_destroy(value); }
                      };
                      wl_resource_set_implementation(
                          token, &impl, peer.requests.back().get(), [](wl_resource* resource) {
                            static_cast<Request*>(wl_resource_get_user_data(resource))->token = nullptr;
                          }
                      );
                    },
                .activate = [](wl_client*, wl_resource*, const char*, wl_resource*) { TEST_CHECK(false); }
            };
            wl_resource_set_implementation(resource, &impl, data, nullptr);
          }
      );
      int fds[2];
      TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
      TEST_CHECK(wl_client_create(m_server, fds[0]));
      m_client = wl_display_connect_to_fd(fds[1]);
      TEST_CHECK(m_client);
      m_registry = wl_display_get_registry(m_client);
      static const wl_registry_listener listener{
          .global =
              [](void* data, wl_registry* registry, std::uint32_t id, const char* name, std::uint32_t) {
                auto& peer = *static_cast<Peer*>(data);
                if (std::string_view(name) == "wl_compositor") {
                  peer.m_compositor =
                      static_cast<wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, 1));
                } else if (std::string_view(name) == "wl_seat") {
                  peer.m_seat = static_cast<wl_seat*>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
                } else if (std::string_view(name) == "xdg_activation_v1") {
                  peer.m_manager =
                      static_cast<xdg_activation_v1*>(wl_registry_bind(registry, id, &xdg_activation_v1_interface, 1));
                }
              },
          .global_remove = [](void*, wl_registry*, std::uint32_t) {}
      };
      wl_registry_add_listener(m_registry, &listener, this);
      until([&] { return m_compositor && m_seat && m_manager; });
      m_surface = wl_compositor_create_surface(m_compositor);
      until([&] { return m_source; });
    }
    ~Peer() {
      TEST_CHECK(!m_manager);
      wl_surface_destroy(m_surface);
      wl_compositor_destroy(m_compositor);
      wl_seat_destroy(m_seat);
      wl_registry_destroy(m_registry);
      wl_display_disconnect(m_client);
      wl_display_destroy_clients(m_server);
      wl_display_destroy(m_server);
    }
    auto service() { return std::make_unique<luciole::LaunchActivation>(std::exchange(m_manager, nullptr)); }
    void
    request(luciole::LaunchActivation& service, std::function<void(std::string)> reply, std::uint32_t serial = 42) {
      service.request(m_surface, m_seat, serial, "org.example.Viewer.desktop", std::move(reply));
    }
    void pump() {
      TEST_CHECK(wl_display_flush(m_client) >= 0);
      TEST_CHECK(wl_event_loop_dispatch(wl_display_get_event_loop(m_server), 0) >= 0);
      wl_display_flush_clients(m_server);
      pollfd fd{wl_display_get_fd(m_client), POLLIN, 0};
      if (::poll(&fd, 1, 0) > 0) {
        TEST_CHECK(wl_display_dispatch(m_client) >= 0);
      }
      TimerManager::instance().tick();
    }
    template <typename Predicate> void until(Predicate condition) {
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (!condition() && std::chrono::steady_clock::now() < deadline) {
        pump();
        ::poll(nullptr, 0, 1);
      }
      TEST_CHECK(condition());
    }
    void done(std::size_t index, const char* token) {
      TEST_CHECK(requests[index]->token);
      xdg_activation_token_v1_send_done(requests[index]->token, token);
    }
  };
  void protocol() {
    Peer peer;
    auto service = peer.service();
    std::vector<std::string> results;
    const auto reply = [&](std::string value) { results.push_back(std::move(value)); };
    peer.request(*service, reply, 0);
    TEST_CHECK(results == std::vector<std::string>{""} && peer.requests.empty());
    results.clear();
    peer.request(*service, reply);
    peer.request(*service, reply);
    TEST_CHECK(results.empty());
    peer.until([&] { return peer.requests.size() == 2 && peer.requests[1]->committed; });
    for (const auto& request : peer.requests) {
      TEST_CHECK(request->serial == 42 && request->seat && request->source && request->appId == "org.example.Viewer");
    }
    peer.done(1, "second");
    peer.done(0, "first");
    peer.until([&] { return results.size() == 2; });
    TEST_CHECK((results == std::vector<std::string>{"second", "first"}));
    peer.until([&] { return !peer.requests[0]->token && !peer.requests[1]->token; });
    peer.request(*service, reply);
    peer.until([&] { return results.size() == 3; });
    TEST_CHECK(results.back().empty()); // An unresponsive compositor cannot stall launching.
    peer.until([&] { return !peer.requests[2]->token; });
    peer.request(*service, reply);
    peer.until([&] { return peer.requests.size() == 4; });
    service.reset();
    TEST_CHECK(results.size() == 4 && results.back().empty());
    peer.until([&] { return !peer.requests[3]->token; });
  }
  void callbackTeardown() {
    Peer peer;
    auto service = peer.service();
    int replies = 0;
    for (int i = 0; i < 32; ++i) {
      peer.request(*service, [&](const auto&) { ++replies; });
    }
    peer.request(*service, [&](const auto& token) {
      TEST_CHECK(token.empty());
      ++replies;
    });
    TEST_CHECK(replies == 1);
    peer.until([&] { return peer.requests.size() == 32; });
    service.reset();
    TEST_CHECK(replies == 33);
    peer.until([&] { return std::ranges::all_of(peer.requests, [](const auto& request) { return !request->token; }); });
  }
  void selfRelease() {
    Peer peer;
    auto service = peer.service();
    bool completed = false;
    peer.request(*service, [&](const auto& token) {
      TEST_CHECK(token == "release");
      service.reset();
      completed = true;
    });
    peer.until([&] { return peer.requests.size() == 1; });
    peer.done(0, "release");
    peer.until([&] { return completed; });
    peer.until([&] { return !peer.requests[0]->token; });
  }
  void contexts() {
    g_setenv("XDG_ACTIVATION_TOKEN", "inherited-stale", true);
    g_setenv("DESKTOP_STARTUP_ID", "inherited-startup", true);
    const auto first = luciole::launchContext("fresh-one"), second = luciole::launchContext("fresh-two"),
               fallback = luciole::launchContext({});
    const luciole::GioPtr<GAppInfo> app(
        g_app_info_create_from_commandline("true %U", "fixture", G_APP_INFO_CREATE_SUPPORTS_URIS, nullptr)
    );
    TEST_CHECK(app);
    const auto check = [&](GAppLaunchContext* context, const char* expected) {
      char** env = g_app_launch_context_get_environment(context);
      const char* token = g_environ_getenv(env, "XDG_ACTIVATION_TOKEN");
      TEST_CHECK(expected ? token && std::string_view(token) == expected : !token);
      TEST_CHECK(!g_environ_getenv(env, "DESKTOP_STARTUP_ID"));
      g_strfreev(env);
      char* id = g_app_launch_context_get_startup_notify_id(context, app.get(), nullptr);
      TEST_CHECK(expected ? id && std::string_view(id) == expected : !id);
      g_free(id);
    };
    check(first.get(), "fresh-one");
    check(second.get(), "fresh-two");
    check(fallback.get(), nullptr);
    TEST_CHECK(std::string_view(g_getenv("XDG_ACTIVATION_TOKEN")) == "inherited-stale");
  }
} // namespace
int main() {
  protocol();
  callbackTeardown();
  selfRelease();
  contexts();
  std::println("native launch activation checks passed");
}
