#include "app/file_manager_service.h"

#include "core/log.h"

#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace {
  constexpr const char* kBusName = "org.freedesktop.FileManager1";
  constexpr const char* kObjectPath = "/org/freedesktop/FileManager1";
  // Matches the default D-Bus call timeout: the activating caller has given up by then.
  constexpr guint kActivationTimeoutSeconds = 25;
  constexpr const char* kIntrospection = R"(<node>
  <interface name="org.freedesktop.FileManager1">
    <method name="ShowFolders">
      <arg type="as" name="URIs" direction="in"/>
      <arg type="s" name="StartupId" direction="in"/>
    </method>
    <method name="ShowItems">
      <arg type="as" name="URIs" direction="in"/>
      <arg type="s" name="StartupId" direction="in"/>
    </method>
    <method name="ShowItemProperties">
      <arg type="as" name="URIs" direction="in"/>
      <arg type="s" name="StartupId" direction="in"/>
    </method>
  </interface>
</node>)";

  const Logger kLog("file-manager-dbus");

  std::optional<luciole::Location> parseLocation(const char* text) {
    try {
      // The specification asks for URIs; tolerate the absolute paths some callers send.
      return g_path_is_absolute(text) ? luciole::Location::fromPath(text) : luciole::Location::fromUri(text);
    } catch (const std::exception& error) {
      kLog.warn("ignoring '{}': {}", text, error.what());
      return std::nullopt;
    }
  }
} // namespace

luciole::FileManagerService::FileManagerService(Handler handler, bool activated)
    : m_handler(std::move(handler)), m_node(g_dbus_node_info_new_for_xml(kIntrospection, nullptr)),
      m_awaiting(activated) {
  m_owner = g_bus_own_name(
      G_BUS_TYPE_SESSION, kBusName, activated ? G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE : G_BUS_NAME_OWNER_FLAGS_NONE,
      busAcquired, nullptr, nameLost, this, nullptr
  );
  if (activated) {
    m_timeout = g_timeout_add_seconds(
        kActivationTimeoutSeconds,
        [](gpointer data) -> gboolean {
          auto* self = static_cast<FileManagerService*>(data);
          self->m_timeout = 0;
          self->m_awaiting = false;
          return G_SOURCE_REMOVE;
        },
        this
    );
  }
}

luciole::FileManagerService::~FileManagerService() {
  release();
  g_dbus_node_info_unref(m_node);
}

void luciole::FileManagerService::release() {
  stopAwaiting();
  if (m_registration != 0) {
    g_dbus_connection_unregister_object(m_connection, m_registration);
    m_registration = 0;
  }
  if (m_owner != 0) {
    g_bus_unown_name(m_owner);
    m_owner = 0;
  }
  if (m_connection != nullptr) {
    g_object_unref(m_connection);
    m_connection = nullptr;
  }
}

void luciole::FileManagerService::stopAwaiting() {
  m_awaiting = false;
  if (m_timeout != 0) {
    g_source_remove(m_timeout);
    m_timeout = 0;
  }
}

void luciole::FileManagerService::busAcquired(GDBusConnection* connection, const char* /*name*/, gpointer data) {
  auto* self = static_cast<FileManagerService*>(data);
  static const GDBusInterfaceVTable vtable = {
      .method_call = methodCall, .get_property = nullptr, .set_property = nullptr, .padding = {}
  };
  GError* error = nullptr;
  self->m_registration = g_dbus_connection_register_object(
      connection, kObjectPath, self->m_node->interfaces[0], &vtable, self, nullptr, &error
  );
  if (self->m_registration == 0) {
    kLog.warn("cannot export {}: {}", kObjectPath, error->message);
    g_error_free(error);
    return;
  }
  self->m_connection = G_DBUS_CONNECTION(g_object_ref(connection));
}

void luciole::FileManagerService::nameLost(GDBusConnection* connection, const char* /*name*/, gpointer data) {
  auto* self = static_cast<FileManagerService*>(data);
  if (connection == nullptr) {
    kLog.debug("no session bus; FileManager1 requests are unavailable");
  }
  // A regular instance stays queued and takes over when the current owner exits.
  // An activated instance was started for one call that another owner now receives.
  if (self->m_awaiting) {
    self->stopAwaiting();
  }
}

void luciole::FileManagerService::methodCall(
    GDBusConnection* /*connection*/, const char* /*sender*/, const char* /*path*/, const char* /*interface*/,
    const char* method, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data
) {
  auto* self = static_cast<FileManagerService*>(data);
  FileManagerRequest request;
  const std::string_view name(method);
  if (name == "ShowFolders") {
    request.kind = FileManagerRequest::Kind::Folders;
  } else if (name == "ShowItems") {
    request.kind = FileManagerRequest::Kind::Items;
  } else if (name == "ShowItemProperties") {
    request.kind = FileManagerRequest::Kind::ItemProperties;
  } else {
    g_dbus_method_invocation_return_error(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method
    );
    return;
  }
  const char** uris = nullptr;
  const char* startupId = nullptr;
  g_variant_get(parameters, "(^a&s&s)", &uris, &startupId);
  for (const char** uri = uris; *uri != nullptr; ++uri) {
    if (auto location = parseLocation(*uri)) {
      request.locations.push_back(std::move(*location));
    }
  }
  g_free(static_cast<gpointer>(uris));
  if (request.locations.empty()) {
    g_dbus_method_invocation_return_error_literal(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No valid URI to show"
    );
  } else {
    // Exceptions must not unwind through GLib's C frames.
    try {
      self->m_handler(std::move(request));
      g_dbus_method_invocation_return_value(invocation, nullptr);
    } catch (const std::exception& error) {
      kLog.warn("{} failed: {}", method, error.what());
      g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED, error.what());
    }
  }
  self->stopAwaiting();
}
