#include "fs/background_job.h"

#include "fs/gio_ptr.h"

#include <gio/gio.h>

void luciole::detail::runInThread(std::move_only_function<void()> work, std::move_only_function<void()> done) {
  struct Request {
    std::move_only_function<void()> work;
    std::move_only_function<void()> done;
  };
  const GioPtr<GTask> task(g_task_new(
      nullptr, nullptr,
      [](GObject*, GAsyncResult* result, gpointer) {
        // GTask's worker reference may outlive delivery. Release completion captures
        // before returning, so finished services cannot retain hidden worker ownership.
        auto done = std::move(static_cast<Request*>(g_task_get_task_data(G_TASK(result)))->done);
        done();
      },
      nullptr
  ));
  g_task_set_task_data(task.get(), new Request{std::move(work), std::move(done)}, [](gpointer data) {
    delete static_cast<Request*>(data);
  });
  g_task_run_in_thread(task.get(), [](GTask* task, gpointer, gpointer data, GCancellable*) {
    {
      auto work = std::move(static_cast<Request*>(data)->work);
      work();
    }
    g_task_return_boolean(task, TRUE);
  });
}
