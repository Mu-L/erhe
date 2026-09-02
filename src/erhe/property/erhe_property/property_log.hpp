#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::property {

extern std::shared_ptr<spdlog::logger> log;

void initialize_logging();

} // namespace erhe::property
