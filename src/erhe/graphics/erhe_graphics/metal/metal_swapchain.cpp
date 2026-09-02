#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <cstring>

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&  device_impl,
    Surface_impl& surface_impl
)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    clear_current_drawable();
    if (m_capture_command_buffer != nullptr) {
        m_capture_command_buffer->release();
        m_capture_command_buffer = nullptr;
    }
    if (m_capture_buffer != nullptr) {
        m_capture_buffer->release();
        m_capture_buffer = nullptr;
    }
}

auto Swapchain_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    static_cast<void>(frame_begin_info);

    CA::MetalLayer* layer = m_surface_impl.get_metal_layer();
    if (layer == nullptr) {
        return false;
    }

    // nextDrawable() is autoreleased. Take a reference: the drawable is
    // used later in the frame (render pass setup) and at submit time
    // (presentDrawable), and holding it explicitly keeps it alive
    // independently of which autorelease pool happens to be open.
    clear_current_drawable();
    m_current_drawable = layer->nextDrawable();
    if (m_current_drawable != nullptr) {
        m_current_drawable->retain();
    }
    return m_current_drawable != nullptr;
}

void Swapchain_impl::clear_current_drawable()
{
    if (m_current_drawable != nullptr) {
        m_current_drawable->release();
        m_current_drawable = nullptr;
    }
}

auto Swapchain_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    static_cast<void>(frame_end_info);
    clear_current_drawable();
    return true;
}

auto Swapchain_impl::has_depth() const -> bool { return false; }
auto Swapchain_impl::has_stencil() const -> bool { return false; }

auto Swapchain_impl::get_color_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_8_vec4_bgra_srgb;
}

auto Swapchain_impl::get_depth_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_undefined; // swapchain has no depth
}

auto Swapchain_impl::get_current_drawable() const -> CA::MetalDrawable*
{
    return m_current_drawable;
}

auto Swapchain_impl::is_capture_supported() const -> bool
{
    CA::MetalLayer* layer = m_surface_impl.get_metal_layer();
    return (layer != nullptr) && !layer->framebufferOnly();
}

void Swapchain_impl::request_capture()
{
    m_capture_requested = true;
}

void Swapchain_impl::record_capture(MTL::CommandBuffer* command_buffer)
{
    if (!m_capture_requested) {
        return;
    }
    m_capture_requested = false;
    if ((command_buffer == nullptr) || (m_current_drawable == nullptr) || !is_capture_supported()) {
        return;
    }
    MTL::Texture* texture = m_current_drawable->texture();
    if (texture == nullptr) {
        return;
    }
    // The readback conversion only understands 4x8-bit RGBA / BGRA.
    const MTL::PixelFormat pixel_format = texture->pixelFormat();
    switch (pixel_format) {
        case MTL::PixelFormatBGRA8Unorm:
        case MTL::PixelFormatBGRA8Unorm_sRGB:
        case MTL::PixelFormatRGBA8Unorm:
        case MTL::PixelFormatRGBA8Unorm_sRGB:
            break;
        default:
            log_swapchain->warn("Metal frame capture: unsupported drawable pixel format {}", static_cast<unsigned long>(pixel_format));
            return;
    }

    const uint32_t    width      = static_cast<uint32_t>(texture->width());
    const uint32_t    height     = static_cast<uint32_t>(texture->height());
    const std::size_t byte_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (byte_count == 0) {
        return;
    }

    // A previous capture that was never collected: drop it, its buffer is
    // about to be overwritten anyway.
    if (m_capture_command_buffer != nullptr) {
        m_capture_command_buffer->release();
        m_capture_command_buffer = nullptr;
    }
    if ((m_capture_buffer != nullptr) && (m_capture_buffer->length() < byte_count)) {
        m_capture_buffer->release();
        m_capture_buffer = nullptr;
    }
    if (m_capture_buffer == nullptr) {
        m_capture_buffer = m_device_impl.get_mtl_device()->newBuffer(byte_count, MTL::ResourceStorageModeShared);
        if (m_capture_buffer == nullptr) {
            log_swapchain->warn("Metal frame capture: could not allocate a {} byte readback buffer", byte_count);
            return;
        }
        m_capture_buffer->setLabel(NS::String::string("swapchain capture", NS::UTF8StringEncoding));
    }

    MTL::BlitCommandEncoder* blit = command_buffer->blitCommandEncoder();
    if (blit == nullptr) {
        return;
    }
    blit->copyFromTexture(
        texture,
        0, // slice
        0, // level
        MTL::Origin{0, 0, 0},
        MTL::Size{width, height, 1},
        m_capture_buffer,
        0,                                    // destination offset
        static_cast<NS::UInteger>(width) * 4, // bytes per row
        byte_count                            // bytes per image
    );
    blit->endEncoding();

    m_capture_command_buffer = command_buffer;
    m_capture_command_buffer->retain();
    m_capture_width        = width;
    m_capture_height       = height;
    m_capture_pixel_format = static_cast<unsigned long>(pixel_format);
    m_capture_frame_index  = m_device_impl.get_frame_index();
}

auto Swapchain_impl::read_back_capture(uint32_t& out_width, uint32_t& out_height, std::vector<std::byte>& out_rgba8) -> bool
{
    if ((m_capture_command_buffer == nullptr) || (m_capture_buffer == nullptr)) {
        return false;
    }

    // Reject a stale capture whose collector went away: the caller re-arms
    // and gets a current frame instead. The normal arm -> render -> collect
    // cycle sees an age of 1.
    const uint64_t age = m_device_impl.get_frame_index() - m_capture_frame_index;
    if (age > 2) {
        m_capture_command_buffer->release();
        m_capture_command_buffer = nullptr;
        return false;
    }

    // The capture rides on a presenting command buffer, which completes
    // once the compositor has consumed the drawable (diagnostic path; one
    // frame of latency at most when presentation is progressing).
    m_capture_command_buffer->waitUntilCompleted();
    const bool completed = (m_capture_command_buffer->status() == MTL::CommandBufferStatusCompleted);
    m_capture_command_buffer->release();
    m_capture_command_buffer = nullptr;
    if (!completed) {
        return false;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(m_capture_width) * static_cast<std::size_t>(m_capture_height);
    const std::byte*  source      = static_cast<const std::byte*>(m_capture_buffer->contents());
    const bool        bgra        =
        (m_capture_pixel_format == static_cast<unsigned long>(MTL::PixelFormatBGRA8Unorm)) ||
        (m_capture_pixel_format == static_cast<unsigned long>(MTL::PixelFormatBGRA8Unorm_sRGB));
    out_rgba8.resize(pixel_count * 4);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::byte* in  = source + i * 4;
        std::byte*       out = out_rgba8.data() + i * 4;
        out[0] = bgra ? in[2] : in[0];
        out[1] = in[1];
        out[2] = bgra ? in[0] : in[2];
        out[3] = std::byte{0xff}; // opaque, like the Vulkan readback
    }
    out_width  = m_capture_width;
    out_height = m_capture_height;
    return true;
}

} // namespace erhe::graphics
