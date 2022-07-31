#include "editor_scenes.hpp"
#include "scene/scene_root.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

Editor_scenes::Editor_scenes()
    : Component{c_label}
{
}

Editor_scenes::~Editor_scenes() noexcept
{
}

void Editor_scenes::register_scene_root(const std::shared_ptr<Scene_root>& scene_root)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    m_scene_roots.push_back(scene_root);
}

void Editor_scenes::update_once_per_frame(const erhe::components::Time_context&)
{
    for (const auto& scene_root : m_scene_roots)
    {
        scene_root->scene().update_node_transforms();
    }
}

[[nodiscard]] auto Editor_scenes::get_scene_roots() -> const std::vector<std::shared_ptr<Scene_root>>&
{
    return m_scene_roots;
}

[[nodiscard]] auto Editor_scenes::get_scene_root() -> const std::shared_ptr<Scene_root>&
{
    return m_scene_root;
}

} // namespace hextiles

