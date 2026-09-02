#include "erhe_file/file_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_log/log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // Same bootstrap as erhe_item_tests: erhe::file::log_file must exist before
    // make_logger() reads the logging configuration through erhe::file.
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    erhe::property::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
