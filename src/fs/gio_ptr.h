#pragma once

#include <glib-object.h>
#include <memory>

namespace luciole {
  struct ObjectUnref {
    void operator()(gpointer object) const { g_object_unref(object); }
  };

  template <typename T> using GioPtr = std::unique_ptr<T, ObjectUnref>;

  struct GFree {
    void operator()(gpointer value) const { g_free(value); }
  };

  using GString = std::unique_ptr<char, GFree>;

  struct GErrorFree {
    void operator()(GError* error) const { g_error_free(error); }
  };

  using GErrorPtr = std::unique_ptr<GError, GErrorFree>;
} // namespace luciole
