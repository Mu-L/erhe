#include "erhe_verify/verify.hpp"

#include <cpptrace/cpptrace.hpp>
#include <cpptrace/formatting.hpp>
#include <cstdio>

namespace {

auto make_formatter() -> cpptrace::formatter
{
    cpptrace::formatter fmt;
    fmt.colors(cpptrace::formatter::color_mode::none);
    fmt.addresses(cpptrace::formatter::address_mode::none);
    return fmt;
}

} // anonymous namespace

void erhe_dump_callstack()
{
    static const cpptrace::formatter fmt = make_formatter();
    fprintf(stderr, "=== Callstack ===\n");
    fmt.print(stderr, cpptrace::generate_trace());
    fprintf(stderr, "=================\n");
}

auto erhe_get_callstack() -> std::string
{
    static const cpptrace::formatter fmt = make_formatter();
    return fmt.format(cpptrace::generate_trace());
}
