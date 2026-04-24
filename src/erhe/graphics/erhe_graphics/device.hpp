#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/generated/graphics_config.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_utility/debug_label.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Format_properties
{
public:
    bool                 supported{false};

    // These are all texture_2d
    bool                 color_renderable{false};
    bool                 depth_renderable{false};
    bool                 stencil_renderable{false};
    bool                 filter{false};
    bool                 framebuffer_blend{false};

    int                  red_size        {0};
    int                  green_size      {0};
    int                  blue_size       {0};
    int                  alpha_size      {0};
    int                  depth_size      {0};
    int                  stencil_size    {0};
    int                  image_texel_size{0};

    std::vector<int>     texture_2d_sample_counts;
    int                  texture_2d_array_max_width{0};
    int                  texture_2d_array_max_height{0};
    int                  texture_2d_array_max_layers{0};

    // These are all texture_2d
    std::vector<int64_t> sparse_tile_x_sizes;
    std::vector<int64_t> sparse_tile_y_sizes;
    std::vector<int64_t> sparse_tile_z_sizes;
};

class Blit_command_encoder;
class Command_encoder;
class Compute_command_encoder;
class Device;
class Render_command_encoder;
class Render_pass;
class Render_pipeline;
class Render_pipeline_create_info;
class Ring_buffer;
class Sampler;
class Surface;
class Swapchain;
class Texture;
class Vulkan_external_creators;

static constexpr unsigned int format_flag_require_depth     = 0x01u;
static constexpr unsigned int format_flag_require_stencil   = 0x02u;
static constexpr unsigned int format_flag_prefer_accuracy   = 0x04u;
static constexpr unsigned int format_flag_prefer_filterable = 0x08u;

// Image_layout is defined in enums.hpp

enum class Texture_heap_path : unsigned int {
    opengl_bindless_textures,   // GL_ARB_bindless_texture: sampler2D(uvec2_handle)
    opengl_sampler_array,       // OpenGL non-bindless: s_texture[handle.x] array
    metal_argument_buffer,      // Metal argument buffers
    vulkan_descriptor_indexing  // Vulkan: erhe_texture_heap[] at set 1 + assigned samplers at set 0
};

class Device_info
{
public:
    Vendor vendor{Vendor::Unknown};

    // Human-readable backend API identifier for diagnostics and window title.
    // Example: "OpenGL 4.1 Core", "Vulkan 1.3.0 VK_DRIVER_ID_MOLTENVK",
    // "Metal (Apple M1)", "Null".
    std::string api_info;

    int  glsl_version           {0};

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
    int  gl_version             {0};
    bool core_profile           {false};
    bool compatibility_profile  {false};
    bool forward_compatible     {false};
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
    uint32_t vulkan_api_version {0};
    uint32_t vulkan_driver_id   {0};
#endif

    bool use_clip_control            {false};
    erhe::math::Coordinate_conventions coordinate_conventions;
    bool use_direct_state_access     {false};
    bool use_binary_shaders          {false};
    bool use_integer_polygon_ids     {false};
    Texture_heap_path texture_heap_path{Texture_heap_path::opengl_sampler_array};

    // Helper queries
    [[nodiscard]] auto uses_bindless_texture       () const -> bool {
        return texture_heap_path == Texture_heap_path::opengl_bindless_textures;
    }

    bool use_sparse_texture          {false};
    bool use_persistent_buffers      {false};
    bool use_multi_draw_indirect_core{false};
    bool use_multi_draw_indirect_arb {false};
    bool emulate_multi_draw_indirect {false};
    bool use_compute_shader          {false};
    bool use_shader_storage_buffers  {false};
    bool use_base_instance           {false};
    bool use_clear_texture           {false};
    bool use_texture_view            {false};
    bool use_debug_output            {false}; // GL 4.3 or ARB_debug_output — debug callback
    bool use_debug_groups            {false}; // GL 4.3 — push/pop debug group (not in ARB_debug_output)

    // limits
    int max_compute_workgroup_count[3] = { 1, 1, 1 };
    int max_compute_workgroup_size [3] = { 1, 1, 1 };
    int max_compute_work_group_invocations       {1};
    int max_compute_shared_memory_size           {1};
    int max_samples                              {0};
    int max_color_texture_samples                {0};
    int max_depth_texture_samples                {0};
    int max_framebuffer_samples                  {0};
    int max_integer_samples                      {0};
    int max_vertex_attribs                       {0};

    int max_texture_size                        {64};
    int max_3d_texture_size                      {0};
    int max_cube_map_texture_size                {0};
    int max_texture_buffer_size                  {0};
    int max_array_texture_layers                 {0};
    int max_sparse_texture_size                  {0};

    uint32_t max_per_stage_descriptor_samplers   {0};
    int max_combined_texture_image_units         {0};  // combined across all shader stages
    int max_uniform_block_size                   {0};
    int max_shader_storage_buffer_bindings       {0};
    int max_uniform_buffer_bindings              {0};
    int max_compute_shader_storage_blocks        {0};
    int max_compute_uniform_blocks               {0};
    int max_vertex_shader_storage_blocks         {0};
    int max_vertex_uniform_blocks                {0};
    int max_vertex_uniform_vectors               {0};
    int max_fragment_shader_storage_blocks       {0};
    int max_fragment_uniform_blocks              {0};
    int max_fragment_uniform_vectors             {0};
    int max_geometry_shader_storage_blocks       {0};
    int max_geometry_uniform_blocks              {0};
    int max_tess_control_shader_storage_blocks   {0};
    int max_tess_control_uniform_blocks          {0};
    int max_tess_evaluation_shader_storage_blocks{0};
    int max_tess_evaluation_uniform_blocks       {0};
    float max_texture_max_anisotropy{1.0f};

    std::vector<int> msaa_sample_counts;
    int max_depth_layers    {4};
    int max_depth_resolution{64};

    // Depth/stencil MSAA resolve capabilities. Bitmasks built from
    // Resolve_mode_flag_bit_mask values. Vulkan: filled from
    // VkPhysicalDeviceDepthStencilResolveProperties. Metal: hardcoded to the
    // OS-version-fixed support set. GL: sample_zero only.
    uint32_t supported_depth_resolve_modes      {Resolve_mode_flag_bit_mask::sample_zero};
    uint32_t supported_stencil_resolve_modes    {Resolve_mode_flag_bit_mask::sample_zero};
    bool     independent_depth_stencil_resolve  {false}; // arbitrary depth/stencil mode pairs allowed
    bool     independent_depth_stencil_resolve_none{false}; // either-but-not-both NONE allowed

    // implementation defined
    unsigned int shader_storage_buffer_offset_alignment{256};
    unsigned int uniform_buffer_offset_alignment       {256};
};

using Shader_error_callback   = std::function<void(const std::string& error_log, const std::string& shader_source, const std::string& callstack)>;
using Device_message_callback = std::function<void(Message_severity severity, const std::string& message, const std::string& callstack)>;
using State_dump_callback     = std::function<void(const std::string& state_dump)>;
using Trace_callback          = std::function<void(const std::string& message)>;

class Frame_state;
class Frame_begin_info;
class Frame_end_info;

class Device_impl;
class Device final
{
public:
    Device(
        const Surface_create_info&      surface_create_info,
        const Graphics_config&          graphics_config,
        Device_message_callback         device_message_callback  = {},
        const Vulkan_external_creators* vulkan_external_creators = nullptr
    );
    Device         (const Device&) = delete;
    void operator= (const Device&) = delete;
    Device         (Device&&)      = delete;
    void operator= (Device&&)      = delete;
    ~Device() noexcept;

    // Device-frame lifecycle (OpenXR-style three-phase). A device frame is
    // one command buffer + one fence, paced through the existing
    // N-frames-in-flight ring. All GPU work (init and rendering) must be
    // issued between begin_frame() and end_frame().
    //
    //   wait_frame  -> analog of xrWaitFrame: pace the ring, recycle prior
    //                  slot resources, fire completion handlers, allocate
    //                  fresh fence. Fill Frame_state with timing info.
    //   begin_frame -> analog of xrBeginFrame: open the device-frame command
    //                  buffer for recording.
    //   end_frame   -> analog of xrEndFrame: end the command buffer, submit,
    //                  advance the frame index. Non-blocking. If a swapchain
    //                  frame was nested, the submit carries acquire/present
    //                  semaphores and vkQueuePresentKHR runs immediately
    //                  after.
    //
    // The Frame_begin_info / Frame_end_info arguments on begin_frame /
    // end_frame are temporary: they will migrate to begin_swapchain_frame /
    // end_swapchain_frame once the editor tick is rewritten (plan step 4).
    // Device-level wait: pace the ring, recycle prior slot resources, fire
    // completion handlers, allocate fresh fence. Does NOT touch the window
    // swapchain. Callers that will engage the swapchain this frame must also
    // call wait_swapchain_frame below.
    [[nodiscard]] auto wait_frame () -> bool;

    // No-args begin_frame / end_frame are the bottom-level primitives: open
    // and close a device frame with no swapchain involvement. Use these
    // around init-time GPU work and around the swapchain-nested rendering
    // phase in the main loop.
    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;

    // Compat wrappers: old editor tick calls begin_frame(info) / end_frame(info).
    // begin_frame(info) = begin_frame() + begin_swapchain_frame(info, dummy).
    // end_frame(info)   = end_swapchain_frame(info) [if active] + end_frame().
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    // Optional swapchain-frame layer; nests inside a recording device frame.
    // Owns only acquire/present semaphores and the acquired image index.
    // wait_swapchain_frame acquires the next image slot on the Vulkan
    // swapchain (and fills Frame_state with predicted display timing).
    // Must be paired with begin_swapchain_frame / end_swapchain_frame each
    // frame that engages the swapchain; skip all three when the desktop
    // window is not being rendered into (e.g. OpenXR).
    [[nodiscard]] auto wait_swapchain_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain_frame(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain_frame  (const Frame_end_info& frame_end_info);

    // Prime the device-frame slot without engaging the swapchain. Use this
    // in place of wait_swapchain_frame when the caller does not render into
    // the desktop window (e.g. OpenXR). Must be called after wait_frame()
    // and before begin_frame(). Must NOT be combined with wait_swapchain_frame
    // in the same tick. No-op on backends that have nothing per-frame to
    // prepare (GL, Metal, null).
    void               prime_device_frame_slot();

    // Blocks until all in-flight device frames complete; flushes pending
    // completion handlers. For init boundaries, not the steady-state loop.
    void wait_idle();

    [[nodiscard]] auto is_in_device_frame   () const -> bool;
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    void start_frame_capture       ();
    void end_frame_capture         ();

    void memory_barrier            (Memory_barrier_mask barriers);
    void clear_texture             (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout (const Texture& texture, Image_layout new_layout);

    // Option B explicit barrier: insert a pipeline barrier that
    // transitions a texture from usage_before to usage_after. Use this
    // between render passes when the writing pass has
    // user_synchronized set in its attachment's usage_after, making
    // the caller responsible for synchronization instead of the
    // automatic end-of-renderpass barrier (Option A).
    //
    // Must be called outside of a render pass (between
    // Scoped_render_pass destructor and the next constructor).
    void cmd_texture_barrier       (uint64_t usage_before, uint64_t usage_after);
    void upload_to_buffer          (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture         (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler    (std::function<void()> callback);
    void on_thread_enter           ();

    [[nodiscard]] auto get_surface                        () -> Surface*;
    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto get_frame_index                    () const -> uint64_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       () -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        () -> Render_command_encoder;
    [[nodiscard]] auto create_render_pipeline             (const Render_pipeline_create_info& create_info) -> std::unique_ptr<Render_pipeline>;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;

    // Probe whether a 2D VK_IMAGE_TILING_OPTIMAL image of the given format with
    // the supplied Image_usage_flag_bit_mask combination can actually be
    // created on this device. On Vulkan this calls
    // vkGetPhysicalDeviceImageFormatProperties2; on other backends it returns
    // true unconditionally (the probe concept is Vulkan-specific). Intended
    // for diagnosing XR swapchain usage-mask issues where the runtime may
    // allocate images with more usage bits than the app requests.
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;

    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_impl                           () -> Device_impl&;
    [[nodiscard]] auto get_impl                           () const -> const Device_impl&;
    [[nodiscard]] auto get_spirv_cache                    () -> Spirv_cache&;
    void               set_shader_error_callback          (Shader_error_callback callback);
    void               set_state_dump_callback            (State_dump_callback callback);
    void               set_trace_callback                 (Trace_callback callback);
    void               shader_error                       (const std::string& error_log, const std::string& shader_source);
    void               device_message                     (Message_severity severity, const std::string& message);
    void               state_dump                         (const std::string& dump);
    void               trace                              (const std::string& message);

    [[nodiscard]] auto get_active_render_pass             () const -> Render_pass*;
    void               set_active_render_pass             (Render_pass* render_pass);

private:
    Device_message_callback      m_device_message_callback{};
    std::unique_ptr<Device_impl> m_impl;
    Spirv_cache                  m_spirv_cache;
    Shader_error_callback        m_shader_error_callback  {};
    State_dump_callback          m_state_dump_callback    {};
    Trace_callback               m_trace_callback         {};
    Render_pass*                 m_active_render_pass     {nullptr};
};

[[nodiscard]] auto get_depth_clear_value_pointer(bool reverse_depth = true) -> const float *; // reverse_depth ? 0.0f : 1.0f;
[[nodiscard]] auto get_depth_function(Compare_operation depth_function, bool reverse_depth = true) -> Compare_operation;


} // namespace erhe::graphics
