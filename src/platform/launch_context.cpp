#include "platform/launch_context.h"

namespace {
  // GObject type registration requires these C-style names.
  // NOLINTBEGIN(readability-identifier-naming)
  struct LucioleLaunchContext {
    GAppLaunchContext parent;
  };
  struct LucioleLaunchContextClass {
    GAppLaunchContextClass parent;
  };
  G_DEFINE_TYPE(LucioleLaunchContext, luciole_launch_context, G_TYPE_APP_LAUNCH_CONTEXT)
  void luciole_launch_context_init(LucioleLaunchContext*) {}
  void luciole_launch_context_class_init(LucioleLaunchContextClass* type) {
    (void)luciole_launch_context_parent_class;
    G_APP_LAUNCH_CONTEXT_CLASS(type)->get_startup_notify_id = [](GAppLaunchContext* context, GAppInfo*,
                                                                 GList*) -> char* {
      return g_strdup(static_cast<const char*>(g_object_get_data(G_OBJECT(context), "luciole-activation-token")));
    };
  }
  // NOLINTEND(readability-identifier-naming)
} // namespace

luciole::GioPtr<GAppLaunchContext> luciole::launchContext(const std::string& activationToken) {
  GioPtr<GAppLaunchContext> context(G_APP_LAUNCH_CONTEXT(g_object_new(luciole_launch_context_get_type(), nullptr)));
  // Never forward a startup token inherited from an earlier application launch.
  g_app_launch_context_unsetenv(context.get(), "XDG_ACTIVATION_TOKEN");
  g_app_launch_context_unsetenv(context.get(), "DESKTOP_STARTUP_ID");
  if (!activationToken.empty()) {
    g_app_launch_context_setenv(context.get(), "XDG_ACTIVATION_TOKEN", activationToken.c_str());
    g_object_set_data_full(
        G_OBJECT(context.get()), "luciole-activation-token", g_strdup(activationToken.c_str()), g_free
    );
  }
  return context;
}
