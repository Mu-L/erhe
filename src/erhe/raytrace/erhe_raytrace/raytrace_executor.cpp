#include "erhe_raytrace/raytrace_executor.hpp"

namespace erhe::raytrace {

namespace {

Task_spawner g_task_spawner{};

}

void set_task_spawner(Task_spawner spawner)
{
    g_task_spawner = std::move(spawner);
}

auto get_task_spawner() -> const Task_spawner&
{
    return g_task_spawner;
}

} // namespace erhe::raytrace
