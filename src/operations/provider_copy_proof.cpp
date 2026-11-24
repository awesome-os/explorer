#include "operations/provider_copy_proof.h"

#include <algorithm>
#include <memory>
#include <string_view>

namespace {
  using namespace luciole;
  constexpr auto kAttributes = "standard::type,standard::is-symlink,standard::size,standard::symlink-target,"
                               "id::file,id::filesystem,etag::value,time::modified,time::modified-usec,"
                               "unix::mode,unix::uid,unix::gid";
  [[noreturn]] void fail(GError* raw) {
    const GErrorPtr error(raw);
    throw FileErrorException(error ? fileError(*error) : FileError{FileErrorKind::Other, "Provider history failed."});
  }
  std::map<std::string, std::string> attributes(GFileInfo* value) {
    std::map<std::string, std::string> result;
    for (const char* name :
         {"standard::type", "standard::is-symlink", "standard::size", "standard::symlink-target", "id::file",
          "id::filesystem", "etag::value", "time::modified", "time::modified-usec", "unix::mode", "unix::uid",
          "unix::gid"}) {
      if (!g_file_info_has_attribute(value, name)) {
        continue;
      }
      const luciole::GString text(g_file_info_get_attribute_as_string(value, name));
      if (text) {
        result.emplace(name, text.get());
      }
    }
    return result;
  }
  void metadata(const luciole::provider_history::Tree& expected, GFileInfo* current) {
    if (expected.attributes != attributes(current)) {
      throwFileError(FileErrorKind::Changed, "The remote copy or its metadata changed.");
    }
  }
} // namespace

void luciole::provider_history::check(const CancelToken& token) {
  if (token.cancelled()) {
    throwFileError(FileErrorKind::Cancelled, "Remote copy history cancelled.");
  }
}

luciole::GioPtr<GFileInfo>
luciole::provider_history::info(const Location& location, const CancelToken& token, bool missing) {
  check(token);
  GError* error = nullptr;
  GioPtr<GFileInfo> value(g_file_query_info(
      gioFile(location).get(), kAttributes, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, gioCancellable(token), &error
  ));
  if (!value) {
    if (missing && error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_error_free(error);
      return {};
    }
    fail(error);
  }
  return value;
}

std::vector<std::string>
luciole::provider_history::children(const Location& location, const CancelToken& token, std::size_t limit) {
  GError* error = nullptr;
  const GioPtr<GFileEnumerator> reader(g_file_enumerate_children(
      gioFile(location).get(), G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
      gioCancellable(token), &error
  ));
  if (!reader) {
    fail(error);
  }
  std::vector<std::string> result;
  for (;;) {
    check(token);
    const GioPtr<GFileInfo> child(g_file_enumerator_next_file(reader.get(), gioCancellable(token), &error));
    if (!child) {
      if (error) {
        fail(error);
      }
      break;
    }
    const char* name = g_file_info_get_name(child.get());
    if (!name
        || !*name
        || std::string_view(name) == "."
        || std::string_view(name) == ".."
        || std::string_view(name).contains('/')
        || result.size() == limit) {
      throwFileError(FileErrorKind::Changed, "The remote folder contains unrecorded entries.");
    }
    result.emplace_back(name);
  }
  if (!g_file_enumerator_close(reader.get(), gioCancellable(token), &error)) {
    fail(error);
  }
  std::ranges::sort(result);
  return result;
}

luciole::provider_history::Tree
luciole::provider_history::capture(const Location& location, Budget& budget, const CancelToken& token, unsigned depth) {
  check(token);
  if (!budget.entries || depth > 64) {
    throwFileError(FileErrorKind::Unsupported, "Remote copy history exceeds its tree limit.");
  }
  --budget.entries;
  const auto value = info(location, token);
  const auto type = g_file_info_get_file_type(value.get());
  Tree tree{.attributes = attributes(value.get()), .digest = {}, .name = {}, .children = {}, .directory = false};
  if (readIsSymlink(value.get())) {
    if (!g_file_info_has_attribute(value.get(), G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET)) {
      throwFileError(FileErrorKind::Unsupported, "The provider cannot verify symbolic-link contents.");
    }
  } else if (type == G_FILE_TYPE_DIRECTORY) {
    tree.directory = true;
    for (const auto& name : children(location, token, budget.entries)) {
      auto child = capture(location.child(name), budget, token, depth + 1);
      child.name = name;
      tree.children.push_back(std::move(child));
    }
  } else if (type == G_FILE_TYPE_REGULAR) {
    const auto size = g_file_info_get_size(value.get());
    if (size < 0 || static_cast<std::uint64_t>(size) > budget.bytes) {
      throwFileError(FileErrorKind::Unsupported, "Remote copy history exceeds its content-proof limit.");
    }
    budget.bytes -= static_cast<std::uint64_t>(size);
    GError* error = nullptr;
    const GioPtr<GFileInputStream> stream(g_file_read(gioFile(location).get(), gioCancellable(token), &error));
    if (!stream) {
      fail(error);
    }
    const std::unique_ptr<GChecksum, decltype(&g_checksum_free)> checksum(
        g_checksum_new(G_CHECKSUM_SHA256), g_checksum_free
    );
    std::vector<guchar> buffer(128 * 1024);
    goffset read = 0;
    for (;;) {
      check(token);
      const auto count = g_input_stream_read(
          G_INPUT_STREAM(stream.get()), buffer.data(), buffer.size(), gioCancellable(token), &error
      );
      if (count < 0) {
        fail(error);
      }
      if (count == 0) {
        break;
      }
      if (count > size - read) {
        throwFileError(FileErrorKind::Changed, "The remote file grew while being checked.");
      }
      read += count;
      g_checksum_update(checksum.get(), buffer.data(), count);
    }
    if (!g_input_stream_close(G_INPUT_STREAM(stream.get()), gioCancellable(token), &error)) {
      fail(error);
    }
    if (read != size) {
      throwFileError(FileErrorKind::Changed, "The remote file changed while being checked.");
    }
    tree.digest = g_checksum_get_string(checksum.get());
  } else {
    throwFileError(FileErrorKind::Unsupported, "Remote copy history does not support special files.");
  }
  metadata(tree, info(location, token).get());
  return tree;
}

void luciole::provider_history::verify(const Location& location, const Tree& tree, const CancelToken& token) {
  metadata(tree, info(location, token).get());
  Budget budget;
  if (capture(location, budget, token) != tree) {
    throwFileError(FileErrorKind::Changed, "The remote copy contents changed. History is no longer valid.");
  }
}

void luciole::provider_history::erase(const Location& location, const CancelToken& token) {
  check(token);
  GError* error = nullptr;
  if (!g_file_delete(gioFile(location).get(), gioCancellable(token), &error)) {
    fail(error);
  }
}

void luciole::provider_history::remove(const Location& location, const Tree& tree, const CancelToken& token) {
  check(token);
  metadata(tree, info(location, token).get());
  for (const auto& child : tree.children) {
    remove(location.child(child.name), child, token);
  }
  if (tree.directory && !children(location, token, 1).empty()) {
    throwFileError(FileErrorKind::Changed, "Unknown entries remain in remote history storage.");
  }
  erase(location, token);
}

void luciole::provider_history::move(const Location& from, const Location& to, const CancelToken& token) {
  check(token);
  GError* error = nullptr;
  if (!g_file_move(
          gioFile(from).get(), gioFile(to).get(),
          static_cast<GFileCopyFlags>(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_NO_FALLBACK_FOR_MOVE),
          gioCancellable(token), nullptr, nullptr, &error
      )) {
    fail(error);
  }
}
