#pragma once

#include "erhe_utility/debug_label.hpp"
#include "erhe_utility/pimpl_ptr.hpp"

#include <string_view>

class Device;

namespace erhe::graphics {

class Scoped_debug_group_impl;
class Scoped_debug_group final
{
public:
    template<std::size_t N>
    explicit Scoped_debug_group(const char (&debug_label)[N])
        : Scoped_debug_group{erhe::utility::Debug_label{std::string_view{debug_label, N - 1}}}
    {
    }

    explicit Scoped_debug_group(erhe::utility::Debug_label debug_label);

    ~Scoped_debug_group() noexcept;

private:
    erhe::utility::pimpl_ptr<Scoped_debug_group_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
