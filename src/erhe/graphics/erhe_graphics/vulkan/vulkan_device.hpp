#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include "volk.h"
// vma forward declaration
VK_DEFINE_HANDLE(VmaAllocator)

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace erhe::graphics {

class Instance_layers
{
public:
    bool m_VK_LAYER_AMD_switchable_graphics {false};
    bool m_VK_LAYER_OBS_HOOK                {false};
    bool m_VK_LAYER_RENDERDOC_Capture       {false};
    bool m_VK_LAYER_LUNARG_api_dump         {false};
    bool m_VK_LAYER_LUNARG_gfxreconstruct   {false};
    bool m_VK_LAYER_KHRONOS_synchronization2{false};
    bool m_VK_LAYER_KHRONOS_validation      {false};
    bool m_VK_LAYER_LUNARG_monitor          {false};
    bool m_VK_LAYER_LUNARG_screenshot       {false};
    bool m_VK_LAYER_KHRONOS_profiles        {false};
    bool m_VK_LAYER_KHRONOS_shader_object   {false};
    bool m_VK_LAYER_LUNARG_crash_diagnostic {false};
};
class Instance_extensions
{
public:
    bool m_VK_KHR_get_physical_device_properties2{false};
    bool m_VK_KHR_get_surface_capabilities2      {false};
    bool m_VK_KHR_surface                        {false};
    bool m_VK_KHR_surface_maintenance1           {false};
    bool m_VK_EXT_surface_maintenance1           {false};
    bool m_VK_KHR_win32_surface                  {false};
    bool m_VK_EXT_debug_report                   {false};
    bool m_VK_EXT_debug_utils                    {false};
    bool m_VK_EXT_swapchain_colorspace           {false};
};
class Device_extensions
{
public:
    bool m_VK_KHR_swapchain                     {false};
    bool m_VK_EXT_swapchain_maintenance1        {false};
    bool m_VK_KHR_swapchain_maintenance1        {false};
    bool m_VK_KHR_present_mode_fifo_latest_ready{false};
    bool m_VK_EXT_present_mode_fifo_latest_ready{false};
    bool m_VK_EXT_device_address_binding_report {false};
    bool m_VK_KHR_load_store_op_none            {false};
    bool m_VK_EXT_load_store_op_none            {false};
    bool m_VK_KHR_push_descriptor               {false};
};
class Capabilities
{
public:
    bool m_present_mode_fifo_latest_ready{false};
    bool m_surface_capabilities2         {false};
    bool m_surface_maintenance1          {false};
    bool m_swapchain_maintenance1        {false};
};

class Frame_begin_info;
class Frame_end_info;
class Device_sync_pool;
class Render_pass_impl;
class Ring_buffer;
class Surface_impl;
class Swapchain;
class Vulkan_immediate_commands;

class Device_frame_in_flight
{
public:
    VkFence         submit_fence  {VK_NULL_HANDLE};
    VkCommandPool   command_pool  {VK_NULL_HANDLE};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
};

// Device-frame lifecycle state (OpenXR-style three phases, plus an
// optional nested swapchain-frame layer). Device_impl transitions through
// these as wait_frame/begin_frame/begin_swapchain_frame/... are called.
//
//   idle               --wait_frame()--------> waited
//   waited             --begin_frame(info)---> in_swapchain_frame (compat: combined)
//   waited             --begin_frame()-------> recording          (future: device-only)
//   recording          --begin_swapchain_frame--> in_swapchain_frame
//   in_swapchain_frame --end_swapchain_frame----> recording
//   recording          --end_frame()---------> idle               (future)
//   in_swapchain_frame --end_frame(info)-----> idle               (compat: combined)
enum class Device_frame_state : uint8_t
{
    idle,
    waited,
    recording,
    in_swapchain_frame
};

class Device;
class Device_impl final
{
public:
    Device_impl(
        Device&                         device,
        const Surface_create_info&      surface_create_info,
        const Graphics_config&          graphics_config,
        const Vulkan_external_creators* vulkan_external_creators = nullptr
    );
    Device_impl   (const Device_impl&) = delete;
    void operator=(const Device_impl&) = delete;
    Device_impl   (Device_impl&&)      = delete;
    void operator=(Device_impl&&)      = delete;
    ~Device_impl  () noexcept;

    [[nodiscard]] auto wait_frame () -> bool;
    [[nodiscard]] auto begin_frame() -> bool;
    [[nodiscard]] auto end_frame  () -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;

    [[nodiscard]] auto wait_swapchain_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_swapchain_frame(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool;
    void               end_swapchain_frame  (const Frame_end_info& frame_end_info);

    // Prime the current frame's device-frame slot: wait on its prior submit
    // fence (if any), recycle the fence + command pool, allocate a fresh fence,
    // ensure its command buffer is allocated. Alternative to wait_swapchain_frame
    // when the caller does not engage the desktop swapchain (e.g. OpenXR).
    // Call after wait_frame() and before begin_frame(). Calling both this and
    // wait_swapchain_frame in the same frame is a misuse (double-prime).
    void               prime_device_frame_slot();
    void               wait_idle            ();
    [[nodiscard]] auto is_in_device_frame   () const -> bool;
    [[nodiscard]] auto is_in_swapchain_frame() const -> bool;

    void start_frame_capture();
    void end_frame_capture  ();

    // Active render pass tracking
    [[nodiscard]] static auto get_device_impl            () -> Device_impl*;
    [[nodiscard]] auto get_active_command_buffer         () const -> VkCommandBuffer;

    // Returns the command buffer of the device frame currently being
    // recorded. VK_NULL_HANDLE if no device frame is active or the slot
    // doesn't have a cb yet. Use this (not get_active_command_buffer)
    // when you want to append commands to the whole device frame rather
    // than to the innermost active render pass.
    [[nodiscard]] auto get_device_frame_command_buffer   () const -> VkCommandBuffer;

    [[nodiscard]] auto get_active_render_pass            () const -> VkRenderPass;
    [[nodiscard]] auto get_active_render_pass_impl       () const -> Render_pass_impl*;
                  void set_active_render_pass_impl       (Render_pass_impl* render_pass_impl);

    void memory_barrier           (Memory_barrier_mask barriers);
    void clear_texture            (const Texture& texture, std::array<double, 4> clear_value);
    void transition_texture_layout(const Texture& texture, Image_layout new_layout);
    void cmd_texture_barrier      (uint64_t usage_before, uint64_t usage_after);
    void upload_to_buffer         (const Buffer& buffer, size_t offset, const void* data, size_t length);
    void upload_to_texture        (const Texture& texture, int level, int x, int y, int width, int height, erhe::dataformat::Format pixelformat, const void* data, int row_stride);
    void add_completion_handler   (std::function<void(Device_impl&)> callback);
    void on_thread_enter          ();

    [[nodiscard]] auto get_handle                         (const Texture& texture, const Sampler& sampler) const -> uint64_t;
    [[nodiscard]] auto create_dummy_texture               (const erhe::dataformat::Format format) -> std::shared_ptr<Texture>;
    [[nodiscard]] auto get_buffer_alignment               (Buffer_target target) -> std::size_t;
    [[nodiscard]] auto allocate_ring_buffer_entry         (Buffer_target buffer_target, Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    [[nodiscard]] auto make_blit_command_encoder          () -> Blit_command_encoder;
    [[nodiscard]] auto make_compute_command_encoder       () -> Compute_command_encoder;
    [[nodiscard]] auto make_render_command_encoder        () -> Render_command_encoder;
    [[nodiscard]] auto get_format_properties              (erhe::dataformat::Format format) const -> Format_properties;
    [[nodiscard]] auto probe_image_format_support         (erhe::dataformat::Format format, uint64_t usage_mask) const -> bool;
    [[nodiscard]] auto get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>;
                  void sort_depth_stencil_formats         (std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const;
    [[nodiscard]] auto choose_depth_stencil_format        (const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format;
    [[nodiscard]] auto choose_depth_stencil_format        (unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_shader_monitor                 () -> Shader_monitor&;
    [[nodiscard]] auto get_info                           () const -> const Device_info&;
    [[nodiscard]] auto get_graphics_config                () const -> const Graphics_config&;
    [[nodiscard]] auto get_allocator                      () -> VmaAllocator&;

    // Return the current device-frame command buffer. All non-swapchain
    // Render_pass_impl instances in a single frame record into this one
    // cb alongside the swapchain render pass and per-frame transfers.
    // The caller must NOT begin, end or submit it -- Device_impl::begin_frame
    // and Device_impl::end_frame handle that.
    [[nodiscard]] auto acquire_shared_command_buffer() -> VkCommandBuffer;

    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const char* label);
    void set_debug_label(VkObjectType object_type, uint64_t object_handle, const std::string& label);

    [[nodiscard]] auto get_sync_pool() -> Device_sync_pool&;

    [[nodiscard]] auto get_device                     () -> Device&;
    [[nodiscard]] auto get_surface                    () -> Surface*;
    [[nodiscard]] auto get_vulkan_instance            () -> VkInstance;
    [[nodiscard]] auto get_vulkan_physical_device     () -> VkPhysicalDevice;
    [[nodiscard]] auto get_vulkan_device              () -> VkDevice;
    [[nodiscard]] auto get_graphics_queue_family_index() const -> uint32_t;
    [[nodiscard]] auto get_present_queue_family_index () const -> uint32_t;
    [[nodiscard]] auto get_graphics_queue             () const -> VkQueue;
    [[nodiscard]] auto get_present_queue              () const -> VkQueue;
    [[nodiscard]] auto get_capabilities               () const -> const Capabilities&;
    [[nodiscard]] auto get_driver_properties          () const -> const VkPhysicalDeviceDriverProperties&;
    [[nodiscard]] auto get_memory_type                (uint32_t memory_type_index) const -> const VkMemoryType&;
    [[nodiscard]] auto get_memory_heap                (uint32_t memory_heap_index) const -> const VkMemoryHeap&;
    [[nodiscard]] auto get_immediate_commands         () -> Vulkan_immediate_commands&;
    [[nodiscard]] auto get_pipeline_cache             () const -> VkPipelineCache;
    [[nodiscard]] auto get_descriptor_set_layout      () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto has_push_descriptor            () const -> bool;
    [[nodiscard]] auto get_texture_set_layout         () const -> VkDescriptorSetLayout;
    [[nodiscard]] auto get_cached_pipeline             (std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto create_graphics_pipeline       (const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto get_or_create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, std::size_t hash) -> VkPipeline;
    [[nodiscard]] auto get_or_create_compatible_render_pass(
        unsigned int                                   color_attachment_count,
        const std::array<erhe::dataformat::Format, 4>& color_attachment_formats,
        erhe::dataformat::Format                       depth_attachment_format,
        erhe::dataformat::Format                       stencil_attachment_format,
        unsigned int                                   sample_count,
        VkPipelineStageFlags                           incoming_src_stage  = 0,
        VkAccessFlags                                  incoming_src_access = 0,
        VkPipelineStageFlags                           incoming_dst_stage  = 0,
        VkAccessFlags                                  incoming_dst_access = 0,
        VkPipelineStageFlags                           outgoing_src_stage  = 0,
        VkAccessFlags                                  outgoing_src_access = 0,
        VkPipelineStageFlags                           outgoing_dst_stage  = 0,
        VkAccessFlags                                  outgoing_dst_access = 0
    ) -> VkRenderPass;
    [[nodiscard]] auto allocate_descriptor_set          () -> VkDescriptorSet;
    void               reset_descriptor_pool            ();

    [[nodiscard]] auto debug_report_callback(
        VkDebugReportFlagsEXT      flags,
        VkDebugReportObjectTypeEXT object_type,
        uint64_t                   object,
        size_t                     location,
        int32_t                    message_code,
        const char*                layer_prefix,
        const char*                message
    ) -> VkBool32;

    [[nodiscard]] auto debug_utils_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
        VkDebugUtilsMessageTypeFlagsEXT             message_types,
        const VkDebugUtilsMessengerCallbackDataEXT* callback_data
    ) -> VkBool32;

    [[nodiscard]] auto get_number_of_frames_in_flight() const -> size_t;
    [[nodiscard]] auto get_frame_index               () const -> uint64_t;
    [[nodiscard]] auto get_frame_in_flight_index     () const -> uint64_t;

    // Per-frame-in-flight submission resources used by Swapchain_impl. Device_impl
    // owns the command pools and their command buffers; the submit_fence slot is
    // filled/recycled by Swapchain_impl (it owns the fence pool).
    [[nodiscard]] auto get_device_frame_in_flight      (size_t index) -> Device_frame_in_flight&;
    void               ensure_device_frame_command_buffer(size_t index);
    void               reset_device_frame_command_pool   (size_t index);
    void               ensure_device_frame_slot         (size_t index);

    // Per-frame descriptor operation counters (for trace logging).
    // Public so Render_command_encoder_impl and Texture_heap_impl
    // can increment them without friend declarations.
    uint32_t m_desc_push_buf_count  {0};
    uint32_t m_desc_push_img_count  {0};
    uint32_t m_desc_alloc_set_count {0};
    uint32_t m_desc_heap_bind_count {0};
    uint32_t m_desc_draw_count      {0};

private:
    static constexpr size_t s_number_of_frames_in_flight = 2;

    void update_frame_completion();

    [[nodiscard]] static auto get_physical_device_score(VkPhysicalDevice vulkan_physical_device, Surface_impl* surface_impl) -> float;
    [[nodiscard]] static auto query_device_queue_family_indices(
        VkPhysicalDevice vulkan_physical_device,
        Surface_impl*    surface_impl,
        uint32_t*        graphics_queue_family_index,
        uint32_t*        present_queue_family_index
    ) -> bool;
    static auto query_device_extensions(
        VkPhysicalDevice          vulkan_physical_device,
        Device_extensions&        device_extensions_out,
        std::vector<const char*>* device_extensions_c_str
    ) -> float;
    [[nodiscard]] auto choose_physical_device(Surface_impl* surface_impl, std::vector<const char*>& device_extensions_c_str) -> bool;

    void frame_completed(uint64_t frame);

    erhe::window::Context_window* m_context_window{nullptr};
    Device&                       m_device;
    Graphics_config               m_graphics_config;
    Shader_monitor                m_shader_monitor;
    Device_info                   m_info;
    class Completion_handler
    {
    public:
        uint64_t                          frame_number;
        std::function<void(Device_impl&)> callback;
    };
    std::vector<Completion_handler> m_completion_handlers;

    std::unique_ptr<Vulkan_immediate_commands> m_immediate_commands;
    std::unique_ptr<Device_sync_pool>          m_sync_pool;

    VkInstance               m_vulkan_instance            {VK_NULL_HANDLE};
    VkPhysicalDevice         m_vulkan_physical_device     {VK_NULL_HANDLE};
    VkDevice                 m_vulkan_device              {VK_NULL_HANDLE};
    VmaAllocator             m_vma_allocator              {VK_NULL_HANDLE};
    VkDebugReportCallbackEXT m_debug_report_callback      {VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debug_utils_messenger      {VK_NULL_HANDLE};
    std::unique_ptr<Surface> m_surface                    {};
    VkQueue                  m_vulkan_graphics_queue      {VK_NULL_HANDLE};
    VkQueue                  m_vulkan_present_queue       {VK_NULL_HANDLE};
    uint32_t                 m_graphics_queue_family_index{0};
    uint32_t                 m_present_queue_family_index {0};

    // Per-frame-in-flight submission resources consumed by Swapchain_impl.
    // Device_impl owns the VkCommandPool + VkCommandBuffer; Swapchain_impl
    // populates/recycles submit_fence from its own fence pool.
    std::array<Device_frame_in_flight, s_number_of_frames_in_flight> m_device_submit_history{};

    VkSemaphore              m_vulkan_frame_end_semaphore {VK_NULL_HANDLE};
    uint64_t                 m_latest_completed_frame     {0}; // GPU
    uint64_t                 m_frame_index                {1}; // CPU

    // Current device-frame lifecycle state. Drives the assertions in
    // wait_frame / begin_frame / begin_swapchain_frame / end_swapchain_frame
    // / end_frame and backs is_in_device_frame / is_in_swapchain_frame.
    Device_frame_state       m_state{Device_frame_state::idle};
    // Set by begin_swapchain_frame on success; read by end_frame to decide
    // whether to drive Swapchain::end_frame (submit+present). Reset after
    // that call and at begin_frame().
    bool                     m_had_swapchain_frame{false};

public:
    // Serializes recording into the active device frame command buffer
    // across worker threads (init taskflow etc). Only the in-frame paths
    // of upload_to_buffer / upload_to_texture / clear_texture /
    // transition_texture_layout and the Blit_command_encoder recording
    // scope need to hold this. At steady-state rendering the tick runs
    // single-threaded so the lock is uncontested.
    std::mutex m_recording_mutex;

private:


    Instance_layers          m_instance_layers    {};
    Instance_extensions      m_instance_extensions{};
    Device_extensions        m_device_extensions  {};
    Capabilities             m_capabilities       {};

    VkPhysicalDeviceDriverProperties              m_driver_properties{};
    VkPhysicalDeviceDepthStencilResolveProperties m_depth_stencil_resolve_properties{};
    VkPhysicalDeviceMemoryProperties2             m_memory_properties{};

    // Pipeline infrastructure
    VkPipelineCache                               m_pipeline_cache           {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_descriptor_set_layout    {VK_NULL_HANDLE};
    VkDescriptorSetLayout                         m_texture_set_layout       {VK_NULL_HANDLE};
    VkDescriptorPool                              m_per_frame_descriptor_pool{VK_NULL_HANDLE};
    std::mutex                                    m_pipeline_map_mutex;
    std::unordered_map<std::size_t, VkPipeline>   m_pipeline_map;
    std::mutex                                    m_compatible_render_pass_mutex;
    std::unordered_map<std::size_t, VkRenderPass> m_compatible_render_pass_map;

    // For ring buffer:
    bool                                      m_need_sync{false};
    std::vector<std::unique_ptr<Ring_buffer>> m_ring_buffers;
    std::size_t                               m_min_buffer_size = 2 * 1024 * 1024; // TODO

    // Active render pass tracking
    static Device_impl*  s_device_impl;
    Render_pass_impl*    m_active_render_pass{nullptr};

    // The command buffer currently open for recording between begin_frame()
    // and end_frame(). Mirrors m_device_submit_history[slot].command_buffer
    // for the active slot while a device frame is in progress; cleared when
    // the device frame ends.
    VkCommandBuffer      m_active_device_frame_command_buffer{VK_NULL_HANDLE};

    // Optional hooks used when erhe::xr wraps Vulkan creation via
    // XR_KHR_vulkan_enable2. Stored as a raw pointer; lifetime is managed by
    // the caller (typically erhe::xr::Headset) and must outlive Device_impl
    // construction.
    const Vulkan_external_creators* m_external_creators{nullptr};
};

} // namespace erhe::graphics
