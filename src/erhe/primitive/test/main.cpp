#include "erhe_file/file_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_primitive/primitive_log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // Same three-step bootstrap as src/erhe/graph/test/main.cpp: sinks first,
    // then erhe::file::log_file by hand (initialize_logging() would read the
    // ini through erhe::file while its own logger is still null), then the
    // library loggers. erhe_primitive code logs, and an uninitialized
    // log_primitive is a null shared_ptr dereference, not a silent no-op.
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    erhe::primitive::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
