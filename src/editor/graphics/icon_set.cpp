#include "icon_set.hpp"
#include "editor_log.hpp"

#include "renderers/programs.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_SVG_LIBRARY_LUNASVG)
#   include <lunasvg.h>
#endif

namespace editor {

Icon_set::Rasterization::Rasterization()
{
}

Icon_set::Rasterization::Rasterization(
    Icon_set& icon_set,
    const int size,
    const int column_count,
    const int row_count
)
    : m_icon_width    {size}
    , m_icon_height   {size}
    , m_icon_uv_width {static_cast<float>(1.0) / static_cast<float>(column_count)}
    , m_icon_uv_height{static_cast<float>(1.0) / static_cast<float>(row_count)}
{
    m_texture = std::make_shared<erhe::graphics::Texture>(
        erhe::graphics::Texture_create_info{
            .target          = gl::Texture_target::texture_2d,
            .internal_format = gl::Internal_format::rgba8,
            .use_mipmaps     = true,
            .width           = column_count * m_icon_width,
            .height          = row_count * m_icon_height
        }
    );
    m_texture->set_debug_label("Icon_set");

    m_texture_handle = erhe::graphics::get_handle(
        *m_texture.get(),
        *icon_set.get<Programs>()->linear_sampler.get()
    );
}

void Icon_set::Rasterization::post_initialize(Icon_set& icon_set)
{
    m_imgui_renderer = icon_set.get<erhe::application::Imgui_renderer>();
}

Icon_set::Icon_set()
    : erhe::components::Component{c_type_name}
{
}

void Icon_set::declare_required_components()
{
    require<erhe::application::Configuration>();
    require<erhe::application::Gl_context_provider>();
    require<Programs>();
}

void Icon_set::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& config = get<erhe::application::Configuration>();
    m_row_count    = 16;
    m_column_count = 16;
    m_row          = 0;
    m_column       = 0;

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    m_small  = Rasterization(*this, config->imgui.small_icon_size, m_column_count, m_row_count);
    m_large  = Rasterization(*this, config->imgui.large_icon_size, m_column_count, m_row_count);
    m_hotbar = Rasterization(*this, config->hotbar.icon_size,      m_column_count, m_row_count);

    const auto icon_directory = std::filesystem::path("res") / "icons";

    icons.camera            = load(icon_directory / "camera.svg");
    icons.directional_light = load(icon_directory / "directional_light.svg");
    icons.drag              = load(icon_directory / "drag.svg");
    icons.spot_light        = load(icon_directory / "spot_light.svg");
    icons.point_light       = load(icon_directory / "point_light.svg");
    icons.pull              = load(icon_directory / "pull.svg");
    icons.push              = load(icon_directory / "push.svg");
    icons.mesh              = load(icon_directory / "mesh.svg");
    icons.move              = load(icon_directory / "move.svg");
    icons.node              = load(icon_directory / "node.svg");
    icons.rotate            = load(icon_directory / "rotate.svg");
    icons.three_dots        = load(icon_directory / "three_dots.svg");
}

void Icon_set::post_initialize()
{
    m_small .post_initialize(*this);
    m_large .post_initialize(*this);
    m_hotbar.post_initialize(*this);
}

void Icon_set::Rasterization::rasterize(
    lunasvg::Document& document,
    const int          column,
    const int          row
)
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    // Render a super sampled icon
    const auto bitmap_ss = document.renderToBitmap(m_icon_width * 4, m_icon_height * 4);

    // Downsample
    const lunasvg::Bitmap bitmap{
        static_cast<uint32_t>(m_icon_width),
        static_cast<uint32_t>(m_icon_height)
    };
    const auto read_stride  = bitmap_ss.stride();
    const auto write_stride = bitmap.stride();
    for (int y = 0; y < m_icon_height; ++y)
    {
        for (int x = 0; x < m_icon_width; ++x)
        {
            float data[4] = { 0, 0, 0, 0};
            for (int ys = 0; ys < 4; ++ys)
            {
                for (int xs = 0; xs < 4; ++xs)
                {
                    const int offset =
                        (
                            (y * 4 + ys) * read_stride +
                            (x * 4 + xs) * 4
                        );
                    for (int c = 0; c < 4; ++c)
                    {
                        uint8_t v = bitmap_ss.data()[offset + c];
                        data[c] += static_cast<float>(v);
                    }
                }
            }
            bitmap.data()[y * write_stride + 4 * x + 0] = static_cast<uint8_t>(data[0] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 1] = static_cast<uint8_t>(data[1] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 2] = static_cast<uint8_t>(data[2] / 16.0f);
            bitmap.data()[y * write_stride + 4 * x + 3] = static_cast<uint8_t>(data[3] / 16.0f);
        }
    }

    const int x_offset = column * m_icon_width;
    const int y_offset = row    * m_icon_height;

    const auto span = gsl::span<std::byte>{
        reinterpret_cast<std::byte*>(bitmap.data()),
        static_cast<size_t>(bitmap.stride()) * static_cast<size_t>(bitmap.height())
    };

    m_texture->upload(
        gl::Internal_format::rgba8,
        span,
        bitmap.width(),
        bitmap.height(),
        1,
        0,
        x_offset,
        y_offset,
        0
    );
#else
    static_cast<void>(document);
    static_cast<void>(column);
    static_cast<void>(row);
#endif
}

auto Icon_set::load(const std::filesystem::path& path) -> glm::vec2
{
#if defined(ERHE_SVG_LIBRARY_LUNASVG)
    Expects(m_row < m_row_count);

    //const auto  current_path = std::filesystem::current_path();
    const auto document = lunasvg::Document::loadFromFile(path.string());
    if (!document)
    {
        log_svg->error("Unable to load {}", path.string());
        return glm::vec2{0.0f, 0.0f};
    }

    const float u = static_cast<float>(m_column) / static_cast<float>(m_column_count);
    const float v = static_cast<float>(m_row   ) / static_cast<float>(m_row_count);

    m_small .rasterize(*document.get(), m_column, m_row);
    m_large .rasterize(*document.get(), m_column, m_row);
    m_hotbar.rasterize(*document.get(), m_column, m_row);

    ++m_column;
    if (m_column >= m_column_count)
    {
        m_column = 0;
        ++m_row;
    }

    return glm::vec2{u, v};
#else
    static_cast<void>(path);
    return glm::vec2{};
#endif
}

auto Icon_set::Rasterization::uv1(const glm::vec2& uv0) const -> glm::vec2
{
    return glm::vec2{
        uv0.x + m_icon_uv_width,
        uv0.y + m_icon_uv_height
    };
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto imvec_from_glm(glm::vec2 v) -> ImVec2
{
    return ImVec2{v.x, v.y};
}
auto imvec_from_glm(glm::vec4 v) -> ImVec4
{
    return ImVec4{v.x, v.y, v.z, v.w};
}
#endif

void Icon_set::Rasterization::icon(
    const glm::vec2 uv0,
    const glm::vec4 tint_color
) const
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
#else
    ERHE_PROFILE_FUNCTION

    m_imgui_renderer->image(
        m_texture,
        m_icon_width,
        m_icon_height,
        uv0,
        uv1(uv0),
        imvec_from_glm(tint_color),
        false
    );
    ImGui::SameLine();
#endif
}

auto Icon_set::Rasterization::icon_button(
    const glm::vec2 uv0,
    const int       frame_padding,
    const glm::vec4 background_color,
    const glm::vec4 tint_color,
    const bool      linear
) const -> bool
{
#if !defined(ERHE_GUI_LIBRARY_IMGUI)
    static_cast<void>(uv0);
    static_cast<void>(tint_color);
    return false;
#else
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(m_imgui_renderer);

    const bool result = m_imgui_renderer->image_button(
        m_texture,
        m_icon_width,
        m_icon_height,
        uv0,
        uv1(uv0),
        frame_padding,
        background_color,
        tint_color,
        linear
    );
    ImGui::SameLine();
    return result;
#endif
}

auto Icon_set::get_icon(const erhe::scene::Light_type type) const -> const glm::vec2
{
    switch (type)
    {
        //using enum erhe::scene::Light_type;
        case erhe::scene::Light_type::spot:        return icons.spot_light;
        case erhe::scene::Light_type::directional: return icons.directional_light;
        case erhe::scene::Light_type::point:       return icons.point_light;
        default: return {};
    }
}

[[nodiscard]] auto Icon_set::get_small_rasterization() const -> const Rasterization&
{
    return m_small;
}

[[nodiscard]] auto Icon_set::get_large_rasterization() const -> const Rasterization&
{
    return m_large;
}

[[nodiscard]] auto Icon_set::get_hotbar_rasterization() const -> const Rasterization&
{
    return m_hotbar;
}

}
