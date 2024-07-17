#pragma once

#include "erhe_item/unique_id.hpp"

#include <string>

namespace editor {

class Editor_context;

class Operation
{
public:
    virtual ~Operation() noexcept;

    virtual void execute (Editor_context& context) = 0;
    virtual void undo    (Editor_context& context) = 0;
    virtual auto describe() const -> std::string = 0;

    [[nodiscard]] inline auto get_serial() const -> std::size_t { return m_id.get_id(); }

private:
    erhe::Unique_id<Operation> m_id{};
};

} // namespace editor
