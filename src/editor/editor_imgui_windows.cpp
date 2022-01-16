﻿#include "editor_imgui_windows.hpp"

#include "configuration.hpp"
#include "editor_view.hpp"
#include "editor_time.hpp"
#include "rendering.hpp"
#include "window.hpp"

#include "graphics/gl_context_provider.hpp"
#include "renderers/imgui_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/geometry/shapes/regular_polygon.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/scene/scene.hpp"

#include <GLFW/glfw3.h>

#include <gsl/gsl>

namespace editor {

using erhe::graphics::Framebuffer;
using erhe::graphics::Texture;

class Scoped_imgui_context
{
public:
    Scoped_imgui_context(ImGuiContext* context)
    {
        m_old_context = ImGui::GetCurrentContext();
        Expects(m_old_context == nullptr);
        ImGui::SetCurrentContext(context);
    }
    ~Scoped_imgui_context()
    {
        ImGui::SetCurrentContext(m_old_context);
    }

private:
    ImGuiContext* m_old_context{nullptr};
};

Rendertarget_imgui_windows::Rendertarget_imgui_windows(
    const std::string_view              name,
    const erhe::components::Components& components,
    const int                           width,
    const int                           height
)
    : m_pipeline_state_tracker{components.get<erhe::graphics::OpenGL_state_tracker>()}
    , m_name                  {name}
    , m_mesh_layer            {"GUI Layer"}
{
    init_rendertarget(width, height);
    init_renderpass  (components);
    init_context     (components);
}

Rendertarget_imgui_windows::~Rendertarget_imgui_windows()
{
    ImGui::DestroyContext(m_imgui_context);
}

void Rendertarget_imgui_windows::init_rendertarget(
    const int width,
    const int height
)
{
    m_texture = std::make_shared<Texture>(
        Texture::Create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .sample_count    = 0,
            .width           = width,
            .height          = height
        }
    );
    m_texture->set_debug_label("ImGui Rendertarget");
    const float clear_value[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    gl::clear_tex_image(
        m_texture->gl_name(),
        0,
        gl::Pixel_format::rgba,
        gl::Pixel_type::float_,
        &clear_value[0]
    );

    Framebuffer::Create_info create_info;
    create_info.attach(gl::Framebuffer_attachment::color_attachment0, m_texture.get());
    m_framebuffer = std::make_unique<Framebuffer>(create_info);
    m_framebuffer->set_debug_label("ImGui Rendertarget");
}

void Rendertarget_imgui_windows::init_renderpass(
    const erhe::components::Components& components
)
{
    using erhe::graphics::Vertex_input_state;
    using erhe::graphics::Input_assembly_state;
    using erhe::graphics::Rasterization_state;
    using erhe::graphics::Depth_stencil_state;
    using erhe::graphics::Color_blend_state;

    const auto& configuration = *components.get<Configuration>().get();
    const auto& programs      = *components.get<Programs     >().get();
    const auto& mesh_memory   = *components.get<Mesh_memory  >().get();
    auto* vertex_input = mesh_memory.vertex_input.get();

    m_renderpass = Renderpass
    {
        .name = "GUI",
        .pipeline =
        {
            .shader_stages  = programs.textured.get(),
            .vertex_input   = vertex_input,
            .input_assembly = &Input_assembly_state::triangles,
            .rasterization  = &Rasterization_state::cull_mode_none,
            .depth_stencil  = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(configuration.reverse_depth),
            .color_blend    = &Color_blend_state::color_blend_premultiplied
        },
        .begin = [this, &programs]()
        {
            const unsigned int gui_texture_unit = 1;
            const unsigned int gui_texture_name = m_texture->gl_name();
            gl::bind_sampler (gui_texture_unit, programs.linear_mipmap_linear_sampler->gl_name());
            gl::bind_textures(gui_texture_unit, 1, &gui_texture_name);
            gl::program_uniform_1i(
                programs.textured->gl_name(),
                programs.gui_sampler_location,
                gui_texture_unit
            );
        },
    };
}


void Rendertarget_imgui_windows::init_context(
    const erhe::components::Components& components
)
{
    IMGUI_CHECKVERSION();

    m_renderer = components.get<Imgui_renderer>();
    auto* font_atlas = m_renderer->get_font_atlas();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    m_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    io.MouseDrawCursor = true;
    io.IniFilename = nullptr; // TODO

    IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");

    io.BackendPlatformUserData = this;
    io.BackendPlatformName     = "erhe rendertarget";
    io.DisplaySize             = ImVec2{static_cast<float>(m_texture->width()), static_cast<float>(m_texture->height())};
    io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};

    io.MousePos                = ImVec2{-FLT_MAX, -FLT_MAX};
    io.MouseHoveredViewport    = 0;
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        io.MouseDown[i] = false;
    }

    // io.AddFocusEvent(focus);

    //io.MouseWheelH += (float)xoffset;
    //io.MouseWheel += (float)yoffset;

    ImGui::SetCurrentContext(nullptr);
}

void Rendertarget_imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_imgui_windows.push_back(window);
}

auto Rendertarget_imgui_windows::add_scene_node(
    Mesh_memory& mesh_memory,
    Scene_root&  scene_root,
    const double dots_per_meter
) -> std::shared_ptr<erhe::scene::Mesh>
{
    auto gui_material = scene_root.make_material("GUI Quad", glm::vec4{0.1f, 0.1f, 0.2f, 1.0f});
    auto gui_geometry = erhe::geometry::shapes::make_rectangle(
        static_cast<double>(m_texture->width()) / dots_per_meter,
        static_cast<double>(m_texture->height()) / dots_per_meter
    );

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto gui_primitive = erhe::primitive::make_primitive(
        gui_geometry,
        mesh_memory.build_info
    );

    erhe::primitive::Primitive primitive{
        .material              = gui_material,
        .gl_primitive_geometry = gui_primitive
    };
    auto gui_mesh = std::make_shared<erhe::scene::Mesh>("GUI Quad", primitive);
    gui_mesh->visibility_mask() = erhe::scene::Node::c_visibility_gui;

    //glm::mat4 m = erhe::toolkit::create_translation(0.0f, 1.0f, 1.0f);
    //gui_mesh->set_parent_from_node(m);

    scene_root.add( // TODO Remove scene_root.gui_layer()?
        gui_mesh,
        scene_root.gui_layer()
    );

    m_mesh_layer.meshes.push_back(gui_mesh);

    return gui_mesh;
}

auto Rendertarget_imgui_windows::texture() const -> std::shared_ptr<erhe::graphics::Texture>
{
    return m_texture;
}

void Rendertarget_imgui_windows::render_mesh_layer(
    Forward_renderer&     forward_renderer,
    const Render_context& context
)
{
    forward_renderer.render(
        {
            .viewport          = context.viewport,
            .camera            = *context.camera,
            .mesh_layers       = { &m_mesh_layer },
            .light_layer       = nullptr, // m_scene_root->light_layer(),
            .materials         = { }, //m_scene_root->materials(),
            .passes            = { &m_renderpass },
            .visibility_filter =
            {
                .require_all_bits_set = erhe::scene::Node::c_visibility_gui
            }
        }
    );
}

void Rendertarget_imgui_windows::imgui_windows()
{
    Scoped_imgui_context scoped_context{m_imgui_context};

    begin_imgui_frame();

    for (auto imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visibile())
        {
            if (imgui_window->begin())
            {
                imgui_window->imgui();
            }
            imgui_window->end();
        }
    }

    end_and_render_imgui_frame();
}

void Rendertarget_imgui_windows::begin_imgui_frame()
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.DeltaTime = 1.0f / 60.0f; // TODO
    //io.MousePos  = ImVec2{-FLT_MAX, -FLT_MAX};;
    //for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    //{
    //    io.MouseDown[i] = 0;
    //}

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Rendertarget_imgui_windows::end_and_render_imgui_frame()
{
    ImGui::EndFrame();
    ImGui::Render();

    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(
        &erhe::graphics::Color_blend_state::color_blend_disabled
    );

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::viewport        (0, 0, m_texture->width(), m_texture->height());
    gl::clear_color     (0.0f, 0.2f, 0.0f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_renderer->render_draw_data();
    gl::generate_texture_mipmap(m_texture->gl_name());
}

Editor_imgui_windows::Editor_imgui_windows()
    : erhe::components::Component{c_name}
{
}

Editor_imgui_windows::~Editor_imgui_windows()
{
    ImGui::DestroyContext(m_imgui_context);
}

void Editor_imgui_windows::connect()
{
    m_editor_rendering       = get    <Editor_rendering>();
    m_pipeline_state_tracker = get    <erhe::graphics::OpenGL_state_tracker>();
    m_renderer               = require<Imgui_renderer>();

    require<Configuration      >();
    require<Gl_context_provider>();
    require<Mesh_memory        >();
    require<Programs           >();
    require<Scene_root         >();
    require<Window             >();
}

void Editor_imgui_windows::initialize_component()
{
    const Scoped_gl_context gl_context{Component::get<Gl_context_provider>()};

    init_context();
}

void Editor_imgui_windows::init_context()
{
    IMGUI_CHECKVERSION();

    auto* font_atlas = m_renderer->get_font_atlas();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    m_renderer->use_as_backend_renderer_on_context(m_imgui_context);

    ImGuiIO& io     = ImGui::GetIO(m_imgui_context);
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    //auto* const glfw_window = reinterpret_cast<GLFWwindow*>(
    //    get<Window>()->get_context_window()->get_glfw_window()
    //);

    io.BackendPlatformUserData = this;
    io.BackendPlatformName = "imgui_impl_erhe";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)
    //io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;          // We can honor io.WantSetMousePos requests (optional, rarely used)
    //io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;    // We can create multi-viewports on the Platform side (optional)

    m_time = 0.0;

    // Keyboard mapping. Dear ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab        ] = erhe::toolkit::Key_tab;
    io.KeyMap[ImGuiKey_LeftArrow  ] = erhe::toolkit::Key_left;
    io.KeyMap[ImGuiKey_RightArrow ] = erhe::toolkit::Key_right;
    io.KeyMap[ImGuiKey_UpArrow    ] = erhe::toolkit::Key_up;
    io.KeyMap[ImGuiKey_DownArrow  ] = erhe::toolkit::Key_down;
    io.KeyMap[ImGuiKey_PageUp     ] = erhe::toolkit::Key_page_up;
    io.KeyMap[ImGuiKey_PageDown   ] = erhe::toolkit::Key_page_down;
    io.KeyMap[ImGuiKey_Home       ] = erhe::toolkit::Key_home;
    io.KeyMap[ImGuiKey_End        ] = erhe::toolkit::Key_end;
    io.KeyMap[ImGuiKey_Insert     ] = erhe::toolkit::Key_insert;
    io.KeyMap[ImGuiKey_Delete     ] = erhe::toolkit::Key_delete;
    io.KeyMap[ImGuiKey_Backspace  ] = erhe::toolkit::Key_backspace;
    io.KeyMap[ImGuiKey_Space      ] = erhe::toolkit::Key_space;
    io.KeyMap[ImGuiKey_Enter      ] = erhe::toolkit::Key_enter;
    io.KeyMap[ImGuiKey_Escape     ] = erhe::toolkit::Key_escape;
    io.KeyMap[ImGuiKey_KeyPadEnter] = erhe::toolkit::Key_kp_enter;
    io.KeyMap[ImGuiKey_A          ] = erhe::toolkit::Key_a;
    io.KeyMap[ImGuiKey_C          ] = erhe::toolkit::Key_c;
    io.KeyMap[ImGuiKey_V          ] = erhe::toolkit::Key_v;
    io.KeyMap[ImGuiKey_X          ] = erhe::toolkit::Key_x;
    io.KeyMap[ImGuiKey_Y          ] = erhe::toolkit::Key_y;
    io.KeyMap[ImGuiKey_Z          ] = erhe::toolkit::Key_z;

    // TODO Clipboard
    // TODO Update monitors

    // Our mouse update function expect PlatformHandle to be filled for the main viewport
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = this;

    ImGui::SetCurrentContext(nullptr);
}

void Editor_imgui_windows::begin_imgui_frame()
{
    const auto& editor_view    = *get<Editor_view>().get();
    const auto& editor_time    = *get<Editor_time>().get();
    const auto& window         = *get<Window     >().get();
    const auto& context_window = window.get_context_window();
    auto*       glfw_window    = context_window->get_glfw_window();

    const auto w = editor_view.width();
    const auto h = editor_view.height();

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.DisplaySize = ImVec2{static_cast<float>(w), static_cast<float>(h)};

    // Setup time step
    const auto frame        = editor_time.frame_number();
    const auto current_time = glfwGetTime();
    io.DeltaTime = m_time > 0.0
        ? static_cast<float>(current_time - m_time)
        : static_cast<float>(1.0 / 60.0);
    m_time = current_time;
    m_frame = frame;

    // ImGui_ImplGlfw_UpdateMousePosAndButtons();
    io.MousePos = ImVec2{-FLT_MAX, -FLT_MAX};
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
    {
        io.MouseDown[i] = m_mouse_just_pressed[i] || glfwGetMouseButton(glfw_window, i) != 0;
        m_mouse_just_pressed[i] = false;
    }
    if (m_has_cursor)
    {
        double mouse_x;
        double mouse_y;
        glfwGetCursorPos(glfw_window, &mouse_x, &mouse_y);
        io.MousePos = ImVec2{static_cast<float>(mouse_x), static_cast<float>(mouse_y)};
    }

    // ImGui_ImplGlfw_UpdateMouseCursor
    const auto cursor = io.MouseDrawCursor
        ? erhe::toolkit::Mouse_cursor_None
        : static_cast<erhe::toolkit::Mouse_cursor>(ImGui::GetMouseCursor());
    context_window->set_cursor(cursor);

    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

void Editor_imgui_windows::end_and_render_imgui_frame()
{
    ImGui::EndFrame();

    if (get<Configuration>()->viewports_hosted_in_imgui_windows)
    {
        ImGui::Render();

        m_editor_rendering->bind_default_framebuffer();
        m_editor_rendering->clear();
        m_renderer->render_draw_data();
    }
}

auto Editor_imgui_windows::create_rendertarget(
    const std::string_view name,
    const int              width,
    const int              height
) -> std::shared_ptr<Rendertarget_imgui_windows>
{
    auto new_rendertarget = std::make_shared<Rendertarget_imgui_windows>(
        name,
        *m_components,
        width,
        height
    );
    m_rendertarget_imgui_windows.push_back(new_rendertarget);
    return new_rendertarget;
}

void Editor_imgui_windows::destroy_rendertarget(
    const std::shared_ptr<Rendertarget_imgui_windows>& rendertarget
)
{
    std::erase_if(
        m_rendertarget_imgui_windows,
        [&rendertarget](const auto& entry)
        {
            return entry == rendertarget;
        }
    );
}

void Editor_imgui_windows::menu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo"      )) {}
            if (ImGui::MenuItem("Redo"      )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Select All")) {}
            if (ImGui::MenuItem("Deselect"  )) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Translate" )) {}
            if (ImGui::MenuItem("Rotate"    )) {}
            if (ImGui::MenuItem("Scale"     )) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Cube"  )) {}
            if (ImGui::MenuItem("Box"   )) {}
            if (ImGui::MenuItem("Sphere")) {}
            if (ImGui::MenuItem("Torus" )) {}
            if (ImGui::MenuItem("Cone"  )) {}
            if (ImGui::BeginMenu("Brush"))
            {
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Modify"))
        {
            if (ImGui::MenuItem("Ambo"    )) {}
            if (ImGui::MenuItem("Dual"    )) {}
            if (ImGui::MenuItem("Truncate")) {}
            ImGui::EndMenu();
        }
        window_menu();
        ImGui::EndMainMenuBar();
    }

    //ImGui::End();
}

void Editor_imgui_windows::window_menu()
{
    //m_frame_log_window->log("menu");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginMenu("Window"))
    {
        for (auto window : m_imgui_windows)
        {
            bool enabled = window->is_visibile();
            if (ImGui::MenuItem(window->title().data(), "", &enabled))
            {
                if (enabled) {
                    window->show();
                }
                else
                {
                    window->hide();
                }
            }
        }
        ImGui::MenuItem("Tool Properties", "", &m_show_tool_properties);
        ImGui::MenuItem("ImGui Style Editor", "", &m_show_style_editor);

        ImGui::Separator();
        if (ImGui::MenuItem("Close All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->hide();
            }
            m_show_tool_properties = false;
        }
        if (ImGui::MenuItem("Open All"))
        {
            for (auto window : m_imgui_windows)
            {
                window->show();
            }
            m_show_tool_properties = true;
        }
        ImGui::EndMenu();
    }

    ImGui::PopStyleVar();
}

auto Editor_imgui_windows::want_capture_mouse() const -> bool
{
    return ImGui::GetIO(m_imgui_context).WantCaptureMouse;
}

void Editor_imgui_windows::register_imgui_window(Imgui_window* window)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_imgui_windows.emplace_back(window);
}

void Editor_imgui_windows::rendertarget_imgui_windows()
{
    for (const auto& rendertarget : m_rendertarget_imgui_windows)
    {
        rendertarget->imgui_windows();
    }
}

void Editor_imgui_windows::imgui_windows()
{
    Scoped_imgui_context scoped_context{m_imgui_context};

    begin_imgui_frame();

    menu();

    for (auto imgui_window : m_imgui_windows)
    {
        if (imgui_window->is_visibile())
        {
            if (imgui_window->begin())
            {
                imgui_window->imgui();
            }
            imgui_window->end();
        }
    }

    //auto priority_action_tool = get_action_tool(m_pointer_context->priority_action());
    //if (
    //    m_show_tool_properties &&
    //    (priority_action_tool != nullptr)
    //)
    //{
    //    if (ImGui::Begin("Tool Properties", &m_show_tool_properties))
    //    {
    //        priority_action_tool->tool_properties();
    //    }
    //    ImGui::End();
    //}

    if (m_show_style_editor)
    {
        if (ImGui::Begin("Dear ImGui Style Editor", &m_show_style_editor))
        {
            ImGui::ShowStyleEditor();
        }
        ImGui::End();
    }

    end_and_render_imgui_frame();
}

void Editor_imgui_windows::render_rendertarget_gui_meshes(
    const Render_context& context
)
{
    auto* forward_renderer = get<Forward_renderer>().get();
    if (forward_renderer == nullptr)
    {
        return;
    }
    for (const auto& rendertarget : m_rendertarget_imgui_windows)
    {
        rendertarget->render_mesh_layer(
            *forward_renderer,
            context
        );
    }
}

void Editor_imgui_windows::on_focus(int focused)
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.AddFocusEvent(focused != 0);
}

void Editor_imgui_windows::on_cursor_enter(int entered)
{
    m_has_cursor = (entered != 0);
}

void Editor_imgui_windows::on_mouse_click(const uint32_t button, const int count)
{
    if (button < ImGuiMouseButton_COUNT && count > 0)
    {
        m_mouse_just_pressed[button] = true;
    }
}

void Editor_imgui_windows::on_mouse_wheel(const double x, const double y)
{
    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    io.MouseWheelH += static_cast<float>(x);
    io.MouseWheel  += static_cast<float>(y);
}

void Editor_imgui_windows::on_key(
    const signed int keycode,
    const bool       pressed
)
{
    using erhe::toolkit::Keycode;
    Scoped_imgui_context scoped_context{m_imgui_context};

    ImGuiIO& io = ImGui::GetIO(m_imgui_context);
    if (keycode >= 0 && keycode < IM_ARRAYSIZE(io.KeysDown))
    {
        if (pressed)
        {
            io.KeysDown[keycode] = true;
            //bd->KeyOwnerWindows[key] = window;
        }
        else
        {
             io.KeysDown[keycode] = false;
        }
    }

    // Modifiers are not reliable across systems
    const signed int left_control  = static_cast<signed int>(erhe::toolkit::Key_left_control);
    const signed int right_control = static_cast<signed int>(erhe::toolkit::Key_right_control);
    const signed int left_shift    = static_cast<signed int>(erhe::toolkit::Key_left_shift);
    const signed int right_shift   = static_cast<signed int>(erhe::toolkit::Key_right_shift);
    const signed int left_alt      = static_cast<signed int>(erhe::toolkit::Key_left_alt);
    const signed int right_alt     = static_cast<signed int>(erhe::toolkit::Key_right_alt);

    io.KeyCtrl  = io.KeysDown[left_control] || io.KeysDown[right_control];
    io.KeyShift = io.KeysDown[left_shift  ] || io.KeysDown[right_shift  ];
    io.KeyAlt   = io.KeysDown[left_alt    ] || io.KeysDown[right_alt    ];
#ifdef _WIN32
    io.KeySuper = false;
#else
    const signed int left_super  = static_cast<signed int>(Keycode::Key_left_super);
    const signed int right_super = static_cast<signed int>(Keycode::Key_right_super);
    io.KeySuper = io.KeysDown[left_super] || io.KeysDown[right_super];
#endif

}

}  // namespace editor