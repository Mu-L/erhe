#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::raytrace {

extern std::shared_ptr<spdlog::logger> log_buffer;
extern std::shared_ptr<spdlog::logger> log_device;
extern std::shared_ptr<spdlog::logger> log_geometry;
extern std::shared_ptr<spdlog::logger> log_instance;
extern std::shared_ptr<spdlog::logger> log_scene;
extern std::shared_ptr<spdlog::logger> log_embree;
extern std::shared_ptr<spdlog::logger> log_frame;

void initialize_logging();

} // namespace erhe::primitive
