#include "task_guard.hpp"

#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_verify/verify.hpp"

#include <taskflow/taskflow.hpp>

namespace editor {

#if !defined(NDEBUG)

// Namespace scope and BEFORE the class: a declaration placed after it would
// not be found by unqualified lookup in the inline member bodies.
//
// Intra-thread nesting means the outer task blocked: every nesting path
// funnels through Executor::_corun_until (Subflow::join, Runtime::corun,
// Executor::corun, tf::TaskGroup), each a blocking call made from inside a
// task. So a nested on_entry on a thread holding a slot is the in-band
// signal that a context-holding task parked - the configuration that wedges
// the fixed-size pool. The depth count does not drift across exceptions:
// TF_EXECUTOR_EXCEPTION_HANDLER catches inside the prologue/epilogue window
// (not so under TF_DISABLE_EXCEPTION_HANDLING, which is not the pin's
// default), and preemption paths fire the prologue only on first entry.
thread_local int t_task_depth = 0;

class Gl_context_task_guard : public tf::ObserverInterface
{
public:
    void set_up(std::size_t) override
    {
    }

    void on_entry(tf::WorkerView, tf::TaskView task_view) override
    {
        if ((t_task_depth++ > 0) && erhe::graphics::thread_holds_worker_context()) {
            const std::source_location* const acquire_site = erhe::graphics::thread_worker_context_acquire_site();
            ERHE_FATAL(
                "task '%s' co-ran on a thread parked while holding GL worker context slot %d (context acquired at %s:%u)",
                task_view.name().c_str(),
                erhe::graphics::thread_worker_context_slot(),
                (acquire_site != nullptr) ? acquire_site->file_name() : "?",
                (acquire_site != nullptr) ? static_cast<unsigned int>(acquire_site->line()) : 0u
            );
        }
    }

    void on_exit(tf::WorkerView, tf::TaskView) override
    {
        --t_task_depth;
    }
};

void attach_gl_context_task_guard(tf::Executor& executor)
{
    static_cast<void>(executor.make_observer<Gl_context_task_guard>());
}

#else

void attach_gl_context_task_guard(tf::Executor&)
{
}

#endif

} // namespace editor
