#pragma once

#include "operations/operation.hpp"

#include <memory>

namespace editor {

class Content_library;
class Content_library_node;

// Moves a content-library entry or folder to another folder of the same
// category (doc/content-library-folders.md D3). Records the node's parent
// and index before and after; execute and undo are one set_parent each,
// under the library mutex, so the detach and the attach happen inside one
// call and the move is never announced as a removal.
class Content_library_move_operation : public Operation
{
public:
    Content_library_move_operation(
        std::shared_ptr<Content_library>      content_library,
        std::shared_ptr<Content_library_node> node,
        std::shared_ptr<Content_library_node> new_parent,
        std::size_t                           new_index
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::shared_ptr<Content_library>      m_content_library;
    std::shared_ptr<Content_library_node> m_node;
    std::shared_ptr<Content_library_node> m_before_parent;
    std::size_t                           m_before_index;
    std::shared_ptr<Content_library_node> m_after_parent;
    std::size_t                           m_after_index;
};

}
