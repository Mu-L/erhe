#include "erhe/scene/scene_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::item
{

std::shared_ptr<spdlog::logger> log;
std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging()
{
    using namespace erhe::log;
    log       = make_logger      ("erhe.item.log");
    log_frame = make_frame_logger("erhe.item.log_frame");
}

} // namespace erhe::item