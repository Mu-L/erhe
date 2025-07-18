#pragma once

#include "erhe_renderer/enums.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_primitive/enums.hpp"
#include "erhe_item/item.hpp"

#include <functional>
#include <string_view>

namespace erhe::renderer { class Pipeline_pass; }
namespace erhe::scene    { using Layer_id = uint64_t; }

namespace editor {

class Render_context;
class Render_style_data;
class Scene_root;

class Composition_pass : public erhe::Item<erhe::Item_base, erhe::Item_base, Composition_pass>
{
public:
    explicit Composition_pass(const Composition_pass&);
    Composition_pass& operator=(const Composition_pass&);
    ~Composition_pass() noexcept override;

    explicit Composition_pass(const std::string_view name);

    virtual void render(const Render_context& context) const;
    void imgui();

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Composition_pass"};
    [[nodiscard]] static auto get_static_type() -> uint64_t;
    auto get_type     () const -> uint64_t         override;
    auto get_type_name() const -> std::string_view override;

    bool                                        enabled{true};
    std::vector<erhe::scene::Layer_id>          mesh_layers;
    std::size_t                                 non_mesh_vertex_count{0};
    std::vector<erhe::renderer::Pipeline_pass*> passes;
    erhe::primitive::Primitive_mode             primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
    erhe::Item_filter                           filter{};
    std::shared_ptr<Scene_root>                 override_scene_root{};
    bool                                        allow_shader_stages_override{true};

    std::optional<erhe::scene_renderer::Primitive_interface_settings>      primitive_settings;
    std::function<void()>                                                  begin;
    std::function<void()>                                                  end; 
    std::function<const Render_style_data&(const Render_context& context)> get_render_style;
};

}
