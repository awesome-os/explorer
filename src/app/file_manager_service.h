#pragma once

#include "fs/location.h"

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <vector>

namespace luciole {
  // One org.freedesktop.FileManager1 call with its URIs already parsed.
  struct FileManagerRequest {
    enum class Kind : std::uint8_t { Folders, Items, ItemProperties };
    Kind kind = Kind::Folders;
    std::vector<Location> locations;
  };

  // Owns org.freedesktop.FileManager1 on the session bus so browsers and portals can reveal files.
  class FileManagerService {
  public:
    using Handler = std::function<void(FileManagerRequest)>;

    // `activated`: the bus started this process for a pending call. It does not queue behind
    // another owner and keeps the process alive until that call arrives or a timeout passes.
    FileManagerService(Handler handler, bool activated);
    ~FileManagerService();
    FileManagerService(const FileManagerService&) = delete;
    FileManagerService& operator=(const FileManagerService&) = delete;

    bool awaitingRequest() const { return m_awaiting; }
    // Hand the name back once no window can show requests; later calls reach another owner.
    void release();

  private:
    static void busAcquired(GDBusConnection* connection, const char* name, gpointer data);
    static void nameLost(GDBusConnection* connection, const char* name, gpointer data);
    static void methodCall(
        GDBusConnection* connection, const char* sender, const char* path, const char* interface, const char* method,
        GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data
    );
    void stopAwaiting();

    Handler m_handler;
    GDBusNodeInfo* m_node = nullptr;
    GDBusConnection* m_connection = nullptr;
    guint m_owner = 0;
    guint m_registration = 0;
    guint m_timeout = 0;
    bool m_awaiting = false;
  };
} // namespace luciole
