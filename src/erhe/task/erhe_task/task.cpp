#include "erhe_task/task.hpp"

#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::task {

void verify_thread_may_spawn(const std::source_location& location)
{
    if (!erhe::graphics::thread_holds_worker_context()) {
        return;
    }
    const std::source_location* const acquire_site = erhe::graphics::thread_worker_context_acquire_site();
    ERHE_FATAL(
        "task spawn at %s:%u on a thread holding GL worker context slot %d (context acquired at %s:%u)",
        location.file_name(),
        static_cast<unsigned int>(location.line()),
        erhe::graphics::thread_worker_context_slot(),
        (acquire_site != nullptr) ? acquire_site->file_name() : "?",
        (acquire_site != nullptr) ? static_cast<unsigned int>(acquire_site->line()) : 0u
    );
}

} // namespace erhe::task
