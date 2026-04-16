#pragma once

#include <memory>

namespace erhe::xr {
    class Xr_action_pose;
}
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace erhe::scene_renderer {
    class Mesh_memory;
}

namespace editor {

class Material_library;
class Scene_root;

class Controller_visualization
{
public:
    Controller_visualization(
        erhe::scene::Node*                 view_root,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        Scene_root&                        scene_root
    );

    [[nodiscard]] auto get_node() const -> erhe::scene::Node*;

    void update(const erhe::xr::Xr_action_pose* pose);

private:
    std::shared_ptr<erhe::scene::Node> m_controller_node;
    std::shared_ptr<erhe::scene::Mesh> m_controller_mesh;
};

}
