#include "erhe_property/property_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::property {

std::shared_ptr<spdlog::logger> log;

void initialize_logging()
{
    using namespace erhe::log;
    log = make_logger("erhe.property.log");
}

} // namespace erhe::property
