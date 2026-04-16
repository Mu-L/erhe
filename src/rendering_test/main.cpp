#include "rendering_test.hpp"

#include <string_view>

auto main(int argc, char** argv) -> int
{
    const std::string_view config_path = (argc > 1)
        ? std::string_view{argv[1]}
        : std::string_view{"config/rendering_test_settings.json"};
    rendering_test::run_rendering_test(config_path);
    return 0;
}
