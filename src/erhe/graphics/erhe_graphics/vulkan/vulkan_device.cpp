#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan_external_creators.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/swapchain.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_debug.hpp"
#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/align.hpp"
#include "erhe_window/renderdoc_capture.hpp"
#include "erhe_window/window.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#if !defined(WIN32)
#   include <csignal>
#endif

#include "erhe_graphics/renderdoc_app.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

// https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html

namespace erhe::graphics {

auto Device_impl::get_device_frame_in_flight(size_t index) -> Device_frame_in_flight&
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    return m_device_submit_history[index];
}

void Device_impl::ensure_device_frame_command_buffer(size_t index)
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    Device_frame_in_flight& device_frame = m_device_submit_history[index];
    if (device_frame.command_pool != VK_NULL_HANDLE) {
        return;
    }

    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_present_queue_family_index
    };
    VkResult result = vkCreateCommandPool(m_vulkan_device, &command_pool_create_info, nullptr, &device_frame.command_pool);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateCommandPool() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    const VkCommandBufferAllocateInfo command_buffer_allocate_info{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = device_frame.command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    result = vkAllocateCommandBuffers(m_vulkan_device, &command_buffer_allocate_info, &device_frame.command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical("vkAllocateCommandBuffer() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    set_debug_label(
        VK_OBJECT_TYPE_COMMAND_BUFFER,
        reinterpret_cast<uint64_t>(device_frame.command_buffer),
        fmt::format("Device frame in flight command buffer {}", index).c_str()
    );
}

void Device_impl::reset_device_frame_command_pool(size_t index)
{
    ERHE_VERIFY(index < m_device_submit_history.size());
    Device_frame_in_flight& device_frame = m_device_submit_history[index];
    if (device_frame.command_pool != VK_NULL_HANDLE) {
        vkResetCommandPool(m_vulkan_device, device_frame.command_pool, 0);
    }
}

auto Device_impl::acquire_shared_command_buffer() -> VkCommandBuffer
{
    // Unified command buffer: merged_into_device_frame records into the
    // same cb that Device_impl::begin_frame opened via vkBeginCommandBuffer.
    // This way offscreen render passes and per-frame transfers (uploads,
    // clears, blits) land in a single submit with the swapchain render
    // pass, in the correct execution order.
    //
    // Note: we bypass get_device_frame_command_buffer() because this is
    // called from Render_pass_impl::start_render_pass BEFORE
    // vkCmdBeginRenderPass runs - the Device_impl::m_active_render_pass
    // flag is already set (for tracking), but the cb is still in
    // recording state, not inside a Vulkan render pass instance yet.
    ERHE_VERIFY(m_state != Device_frame_state::idle);
    const std::size_t slot = static_cast<std::size_t>(get_frame_in_flight_index());
    return m_device_submit_history[slot].command_buffer;
}

Device_impl::~Device_impl() noexcept
{
    vkDeviceWaitIdle(m_vulkan_device);

    for (auto& [hash, pipeline] : m_pipeline_map) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_vulkan_device, pipeline, nullptr);
        }
    }
    m_pipeline_map.clear();
    for (auto& [hash, render_pass] : m_compatible_render_pass_map) {
        if (render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_vulkan_device, render_pass, nullptr);
        }
    }
    m_compatible_render_pass_map.clear();
    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, nullptr);
    }
    if (m_texture_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_vulkan_device, m_texture_set_layout, nullptr);
    }
    if (m_descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_vulkan_device, m_descriptor_set_layout, nullptr);
    }
    if (m_pipeline_cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_vulkan_device, m_pipeline_cache, nullptr);
    }
    // Destroy immediate commands before the device - it owns a command pool,
    // command buffers, fences, and semaphores
    m_immediate_commands.reset();

    // NOTE: This adds completion handlers for destroying related vulkan objects
    m_surface.reset();

    // Sync pool outlives Swapchain_impl: Swapchain's dtor pushes its final
    // acquire/present semaphores back into the pool, so the pool must be
    // destroyed after Swapchain. Still runs before vkDestroyDevice below.
    m_sync_pool.reset();

    // Destroy ring buffers before running completion handlers -- their destructors
    // add completion handlers for VMA deallocation
    m_ring_buffers.clear();

    vkDeviceWaitIdle(m_vulkan_device);

    for (Device_frame_in_flight& device_frame : m_device_submit_history) {
        if (device_frame.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_vulkan_device, device_frame.command_pool, nullptr);
            device_frame.command_pool   = VK_NULL_HANDLE;
            device_frame.command_buffer = VK_NULL_HANDLE;
        }
    }

    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }

    if (m_vulkan_frame_end_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_vulkan_device, m_vulkan_frame_end_semaphore, nullptr);
    }

    // Dump live VMA allocations to help diagnose leaks
    {
        char* stats_string = nullptr;
        vmaBuildStatsString(m_vma_allocator, &stats_string, VK_TRUE);
        if (stats_string != nullptr) {
            log_context->info("VMA stats before destroy:\n{}", stats_string);
            vmaFreeStatsString(m_vma_allocator, stats_string);
        }
    }

    vmaDestroyAllocator(m_vma_allocator);
    if (m_vulkan_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_vulkan_device, nullptr);
    }
    if (m_debug_utils_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_vulkan_instance, m_debug_utils_messenger, nullptr);
    }
    vkDestroyInstance(m_vulkan_instance, nullptr);
    volkFinalize();
}

auto Device_impl::get_pipeline_cache() const -> VkPipelineCache
{
    return m_pipeline_cache;
}

auto Device_impl::get_descriptor_set_layout() const -> VkDescriptorSetLayout
{
    return m_descriptor_set_layout;
}

auto Device_impl::has_push_descriptor() const -> bool
{
    return m_device_extensions.m_VK_KHR_push_descriptor;
}

auto Device_impl::get_texture_set_layout() const -> VkDescriptorSetLayout
{
    return m_texture_set_layout;
}

auto Device_impl::allocate_descriptor_set() -> VkDescriptorSet
{
    if (m_per_frame_descriptor_pool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSetAllocateInfo allocate_info{
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = m_per_frame_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &m_descriptor_set_layout
    };

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_vulkan_device, &allocate_info, &descriptor_set);
    if (result != VK_SUCCESS) {
        log_context->warn("vkAllocateDescriptorSets() failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    ++m_desc_alloc_set_count;
    ERHE_VULKAN_DESC_TRACE("[ALLOC_SET]");
    return descriptor_set;
}

void Device_impl::reset_descriptor_pool()
{
    ERHE_VULKAN_DESC_TRACE(
        "[FRAME_SUMMARY] frame={} push_buf={} push_img={} alloc_set={} heap_binds={} draws={}",
        m_frame_index,
        m_desc_push_buf_count,
        m_desc_push_img_count,
        m_desc_alloc_set_count,
        m_desc_heap_bind_count,
        m_desc_draw_count
    );
    m_desc_push_buf_count  = 0;
    m_desc_push_img_count  = 0;
    m_desc_alloc_set_count = 0;
    m_desc_heap_bind_count = 0;
    m_desc_draw_count      = 0;

    if (m_per_frame_descriptor_pool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_vulkan_device, m_per_frame_descriptor_pool, 0);
    }
    ERHE_VULKAN_DESC_TRACE("[POOL_RESET] frame={}", m_frame_index);
}

auto Device_impl::get_cached_pipeline(const std::size_t hash) -> VkPipeline
{
    std::lock_guard<std::mutex> lock{m_pipeline_map_mutex};
    auto it = m_pipeline_map.find(hash);
    if (it != m_pipeline_map.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

auto Device_impl::create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, const std::size_t hash) -> VkPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(m_vulkan_device, m_pipeline_cache, 1, &create_info, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        log_program->error("vkCreateGraphicsPipelines() failed with {}", static_cast<int32_t>(result));
        return VK_NULL_HANDLE;
    }

    set_debug_label(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline),
        fmt::format("Pipeline hash={:016x}", hash).c_str());

    std::lock_guard<std::mutex> lock{m_pipeline_map_mutex};
    m_pipeline_map[hash] = pipeline;
    return pipeline;
}

auto Device_impl::get_or_create_graphics_pipeline(const VkGraphicsPipelineCreateInfo& create_info, const std::size_t hash) -> VkPipeline
{
    VkPipeline cached = get_cached_pipeline(hash);
    if (cached != VK_NULL_HANDLE) {
        return cached;
    }
    return create_graphics_pipeline(create_info, hash);
}

auto Device_impl::get_or_create_compatible_render_pass(
    const unsigned int                             color_attachment_count,
    const std::array<erhe::dataformat::Format, 4>& color_attachment_formats,
    const erhe::dataformat::Format                 depth_attachment_format,
    const erhe::dataformat::Format                 stencil_attachment_format,
    const unsigned int                             sample_count,
    VkPipelineStageFlags                           incoming_src_stage,
    VkAccessFlags                                  incoming_src_access,
    VkPipelineStageFlags                           incoming_dst_stage,
    VkAccessFlags                                  incoming_dst_access,
    VkPipelineStageFlags                           outgoing_src_stage,
    VkAccessFlags                                  outgoing_src_access,
    VkPipelineStageFlags                           outgoing_dst_stage,
    VkAccessFlags                                  outgoing_dst_access
) -> VkRenderPass
{
    // Compute hash from format tuple and dependency masks
    auto hash_combine = [](std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    std::size_t hash = 0;
    hash_combine(hash, static_cast<std::size_t>(color_attachment_count));
    for (unsigned int i = 0; i < color_attachment_count; ++i) {
        hash_combine(hash, static_cast<std::size_t>(color_attachment_formats[i]));
    }
    hash_combine(hash, static_cast<std::size_t>(depth_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(stencil_attachment_format));
    hash_combine(hash, static_cast<std::size_t>(sample_count));
    hash_combine(hash, static_cast<std::size_t>(incoming_src_stage));
    hash_combine(hash, static_cast<std::size_t>(incoming_src_access));
    hash_combine(hash, static_cast<std::size_t>(incoming_dst_stage));
    hash_combine(hash, static_cast<std::size_t>(incoming_dst_access));
    hash_combine(hash, static_cast<std::size_t>(outgoing_src_stage));
    hash_combine(hash, static_cast<std::size_t>(outgoing_src_access));
    hash_combine(hash, static_cast<std::size_t>(outgoing_dst_stage));
    hash_combine(hash, static_cast<std::size_t>(outgoing_dst_access));

    {
        std::lock_guard<std::mutex> lock{m_compatible_render_pass_mutex};
        auto it = m_compatible_render_pass_map.find(hash);
        if (it != m_compatible_render_pass_map.end()) {
            return it->second;
        }
    }

    // Create a new compatible render pass
    const VkSampleCountFlagBits vk_sample_count = get_vulkan_sample_count(sample_count);

    std::vector<VkAttachmentDescription2> attachments;
    std::vector<VkAttachmentReference2>   color_refs;

    // Color attachments
    for (unsigned int i = 0; i < color_attachment_count; ++i) {
        VkFormat vk_format = to_vulkan(color_attachment_formats[i]);
        attachments.push_back(VkAttachmentDescription2{
            .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
            .pNext          = nullptr,
            .flags          = 0,
            .format         = vk_format,
            .samples        = vk_sample_count,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        });
        color_refs.push_back(VkAttachmentReference2{
            .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .pNext      = nullptr,
            .attachment = static_cast<uint32_t>(attachments.size() - 1),
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        });
    }

    // Depth/stencil attachment
    VkAttachmentReference2 depth_ref{
        .sType      = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        .pNext      = nullptr,
        .attachment = VK_ATTACHMENT_UNUSED,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT
    };
    const bool has_depth   = (depth_attachment_format   != erhe::dataformat::Format::format_undefined);
    const bool has_stencil = (stencil_attachment_format != erhe::dataformat::Format::format_undefined);
    if (has_depth || has_stencil) {
        VkFormat depth_vk_format = has_depth
            ? to_vulkan(depth_attachment_format)
            : to_vulkan(stencil_attachment_format);
        attachments.push_back(VkAttachmentDescription2{
            .sType          = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
            .pNext          = nullptr,
            .flags          = 0,
            .format         = depth_vk_format,
            .samples        = vk_sample_count,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        });
        depth_ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
            (has_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u);
    }

    const VkSubpassDescription2 subpass{
        .sType                   = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
        .pNext                   = nullptr,
        .flags                   = 0,
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .viewMask                = 0,
        .inputAttachmentCount    = 0,
        .pInputAttachments       = nullptr,
        .colorAttachmentCount    = static_cast<uint32_t>(color_refs.size()),
        .pColorAttachments       = color_refs.empty() ? nullptr : color_refs.data(),
        .pResolveAttachments     = nullptr,
        .pDepthStencilAttachment = (depth_ref.attachment != VK_ATTACHMENT_UNUSED) ? &depth_ref : nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments    = nullptr
    };

    // Dependencies must match the actual render passes for validation compatibility.
    // Use caller-provided masks, or derive defaults from attachments.
    if (incoming_src_stage == 0 && incoming_dst_stage == 0 && outgoing_src_stage == 0 && outgoing_dst_stage == 0) {
        incoming_src_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        incoming_src_access = VK_ACCESS_SHADER_READ_BIT;
        outgoing_dst_stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        outgoing_dst_access = VK_ACCESS_SHADER_READ_BIT;
        if (color_attachment_count > 0) {
            incoming_dst_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            incoming_dst_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            outgoing_src_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            outgoing_src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
        if (has_depth || has_stencil) {
            incoming_dst_stage  |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            incoming_dst_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            outgoing_src_stage  |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            outgoing_src_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
    }
    if (incoming_src_stage == 0) {
        incoming_src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (incoming_dst_stage == 0) {
        incoming_dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    if (outgoing_src_stage == 0) {
        outgoing_src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    if (outgoing_dst_stage == 0) {
        outgoing_dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    // The validation layer compares subpass dependency arrays as part of
    // render-pass compatibility, so the compatibility render pass must use
    // the exact same canonical dependencies that the in-use render passes
    // produce. The caller-supplied stage/access masks are intentionally
    // ignored here and only the structural (has_color / has_depth_stencil)
    // bits drive the dependencies.
    static_cast<void>(incoming_src_stage);
    static_cast<void>(incoming_src_access);
    static_cast<void>(incoming_dst_stage);
    static_cast<void>(incoming_dst_access);
    static_cast<void>(outgoing_src_stage);
    static_cast<void>(outgoing_src_access);
    static_cast<void>(outgoing_dst_stage);
    static_cast<void>(outgoing_dst_access);

    VkSubpassDependency2 canonical_dependencies[2];
    make_canonical_subpass_dependencies2(color_attachment_count > 0, has_depth || has_stencil, canonical_dependencies);

    const VkRenderPassCreateInfo2 render_pass_create_info{
        .sType                   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
        .pNext                   = nullptr,
        .flags                   = 0,
        .attachmentCount         = static_cast<uint32_t>(attachments.size()),
        .pAttachments            = attachments.empty() ? nullptr : attachments.data(),
        .subpassCount            = 1,
        .pSubpasses              = &subpass,
        .dependencyCount         = 2,
        .pDependencies           = canonical_dependencies,
        .correlatedViewMaskCount = 0,
        .pCorrelatedViewMasks    = nullptr
    };

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkResult result = vkCreateRenderPass2(m_vulkan_device, &render_pass_create_info, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        log_render_pass->error(
            "vkCreateRenderPass2() for pipeline compatibility failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
        return VK_NULL_HANDLE;
    }

    {
        std::lock_guard<std::mutex> lock{m_compatible_render_pass_mutex};
        m_compatible_render_pass_map[hash] = render_pass;
    }

    return render_pass;
}

auto Device_impl::choose_physical_device(
    Surface_impl*             surface_impl,
    std::vector<const char*>& device_extensions_c_str
) -> bool
{
    VkPhysicalDevice selected_device{VK_NULL_HANDLE};

    if ((m_external_creators != nullptr) && static_cast<bool>(m_external_creators->pick_physical_device)) {
        log_context->info("Picking Vulkan physical device via external (XR) hook");
        const VkResult xr_pick_result = m_external_creators->pick_physical_device(m_vulkan_instance, &selected_device);
        if ((xr_pick_result != VK_SUCCESS) || (selected_device == VK_NULL_HANDLE)) {
            log_context->critical("External physical device pick failed with {} {}", static_cast<int32_t>(xr_pick_result), c_str(xr_pick_result));
            return false;
        }
    } else {
        uint32_t physical_device_count{0};
        VkResult result{VK_SUCCESS};
        result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, nullptr);
        if (result != VK_SUCCESS) {
            log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            return false;
        }
        std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
        if (physical_device_count == 0) {
            log_context->critical("vkEnumeratePhysicalDevices() returned 0 physical devices");
            return false;
        }
        result = vkEnumeratePhysicalDevices(m_vulkan_instance, &physical_device_count, physical_devices.data());
        if (result != VK_SUCCESS) {
            log_context->critical("vkEnumeratePhysicalDevices() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            return false;
        }

        float best_score = std::numeric_limits<float>::lowest();
        for (uint32_t physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index) {
            const VkPhysicalDevice physical_device = physical_devices[physical_device_index];

            const float score = get_physical_device_score(physical_device, surface_impl);
            if (score > best_score) {
                best_score      = score;
                selected_device = physical_device;
            }
        }

        if (selected_device == VK_NULL_HANDLE) {
            return false;
        }
    }

    m_vulkan_physical_device = selected_device;
    const bool queues_ok = query_device_queue_family_indices(
        m_vulkan_physical_device, surface_impl, &m_graphics_queue_family_index, &m_present_queue_family_index
    );
    if (!queues_ok) {
        return false;
    }

    query_device_extensions(m_vulkan_physical_device, m_device_extensions, &device_extensions_c_str);
    return true;
}

auto Device_impl::get_physical_device_score(VkPhysicalDevice vulkan_physical_device, Surface_impl* surface_impl) -> float
{
    VkPhysicalDeviceProperties device_properties{};
    vkGetPhysicalDeviceProperties(vulkan_physical_device, &device_properties);
    const float device_type_score = 0.0f;
    switch (device_properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:          return 101.0f;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 102.0f;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 104.0f;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 103.0f;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return   0.0f;
        default: {
            log_context->warn("Vulkan device type {:4x} not recognized", static_cast<uint32_t>(device_properties.deviceType));
            return 0.0f; // reject device
        }
    }

    const bool queues_ok = query_device_queue_family_indices(vulkan_physical_device, surface_impl, nullptr, nullptr);
    if (!queues_ok) {
        return 0.0f; // reject device
    }

    Device_extensions device_extensions{};
    const float extension_score = query_device_extensions(vulkan_physical_device, device_extensions, nullptr);

    return device_type_score + extension_score;
}

// Check if device meets queue requirements, optionally returns queue family indices
auto Device_impl::query_device_queue_family_indices(
    VkPhysicalDevice vulkan_physical_device,
    Surface_impl*    surface_impl,
    uint32_t*        graphics_queue_family_index_out,
    uint32_t*        present_queue_family_index_out
) -> bool
{
    VkSurfaceKHR vulkan_surface{VK_NULL_HANDLE};
    if (surface_impl != nullptr) {
        if (!surface_impl->can_use_physical_device(vulkan_physical_device)) {
            return false;
        }
        vulkan_surface = surface_impl->get_vulkan_surface();
    }

    uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, nullptr);
    if (queue_family_count == 0) {
        return false;
    }

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device, &queue_family_count, queue_families.data());

    // Require graphics
    // Require present if surface is used
    uint32_t graphics_queue_family_index = UINT32_MAX;
    uint32_t present_queue_family_index  = UINT32_MAX;
    VkResult result{VK_SUCCESS};
    for (uint32_t queue_family_index = 0, end = static_cast<uint32_t>(queue_families.size()); queue_family_index < end; ++queue_family_index) {
        const VkQueueFamilyProperties& queue_family = queue_families[queue_family_index];
        const bool support_graphics = (queue_family.queueCount > 0) && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT);
        const bool support_present = [&]() -> bool
        {
            if (vulkan_surface == VK_NULL_HANDLE) {
                return false;
            }
            VkBool32 support{VK_FALSE};
            result = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device, queue_family_index, vulkan_surface, &support);
            if (result != VK_SUCCESS) {
                log_context->warn("vkGetPhysicalDeviceSurfaceSupportKHR() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            }
            return (result == VK_SUCCESS) && (support == VK_TRUE);
        }();
        if (support_graphics && support_present) {
            graphics_queue_family_index = queue_family_index;
            present_queue_family_index = queue_family_index;
            break;
        }
        if ((graphics_queue_family_index == UINT32_MAX) && support_graphics) {
            graphics_queue_family_index = queue_family_index;
        }
        if ((present_queue_family_index == UINT32_MAX) && support_present) {
            present_queue_family_index = queue_family_index;
        }
    }

    if (graphics_queue_family_index == UINT32_MAX) {
        return false;
    }

    if (
        (surface_impl != nullptr) &&
        (graphics_queue_family_index != present_queue_family_index)
    ) {
        return false;
    }

    if (graphics_queue_family_index_out != nullptr) {
        *graphics_queue_family_index_out = graphics_queue_family_index;
    }
    if (present_queue_family_index_out != nullptr) {
        *present_queue_family_index_out = present_queue_family_index;
    }
    return true;
}

// Gathers available recognized extensions and computes score
auto Device_impl::query_device_extensions(
    VkPhysicalDevice          vulkan_physical_device,
    Device_extensions&        device_extensions_out,
    std::vector<const char*>* device_extensions_c_str
) -> float
{
    float total_score = 0.0f;

    uint32_t device_extension_count{0};
    VkResult result{VK_SUCCESS};
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return 0.0f;
    }
    std::vector<VkExtensionProperties> device_extensions(device_extension_count);
    result = vkEnumerateDeviceExtensionProperties(vulkan_physical_device, nullptr, &device_extension_count, device_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateDeviceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        return 0.0f;
    }

    for (const VkExtensionProperties& extension : device_extensions) {
        log_debug->info("Vulkan Device Extension: {} spec_version {:08x}", extension.extensionName, extension.specVersion);
    }

    // Check device extensions
    auto check_device_extension = [&](const char* name, bool& enable, const float extension_score)
    {
        for (const VkExtensionProperties& extension : device_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                if (device_extensions_c_str != nullptr) {
                    device_extensions_c_str->push_back(name);
                    log_debug->info("  Enabling {}", extension.extensionName);
                    enable = true;
                    total_score += extension_score;
                }
                return;
            }
        }
    };

    check_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME,                      device_extensions_out.m_VK_KHR_swapchain                     , 1.0f);
    check_device_extension(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_KHR_swapchain_maintenance1        , 2.0f);
    check_device_extension(VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME,             device_extensions_out.m_VK_KHR_load_store_op_none            , 2.0f);
    check_device_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,                device_extensions_out.m_VK_KHR_push_descriptor               , 1.0f);
    check_device_extension(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_KHR_present_mode_fifo_latest_ready, 3.0f);

    if (!device_extensions_out.m_VK_KHR_load_store_op_none) {
        check_device_extension(VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,             device_extensions_out.m_VK_EXT_load_store_op_none            , 2.0f);
    }
    if (!device_extensions_out.m_VK_KHR_swapchain_maintenance1) {
        check_device_extension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,        device_extensions_out.m_VK_EXT_swapchain_maintenance1        , 2.0f);
    }
    if (!device_extensions_out.m_VK_KHR_present_mode_fifo_latest_ready) {
        check_device_extension(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME, device_extensions_out.m_VK_EXT_present_mode_fifo_latest_ready, 3.0f);
    }
    return total_score;
}

auto Device_impl::get_device() -> Device&
{
    return m_device;
}

auto Device_impl::get_surface() -> Surface*
{
    return m_surface.get();
}

auto Device_impl::get_vulkan_instance() -> VkInstance
{
    return m_vulkan_instance;
}

auto Device_impl::get_vulkan_physical_device() -> VkPhysicalDevice
{
    return m_vulkan_physical_device;
}

auto Device_impl::get_vulkan_device() -> VkDevice
{
    return m_vulkan_device;
}

auto Device_impl::get_graphics_queue_family_index() const -> uint32_t
{
    return m_graphics_queue_family_index;
}

auto Device_impl::get_present_queue_family_index () const -> uint32_t
{
    return m_present_queue_family_index;
}

auto Device_impl::get_graphics_queue() const -> VkQueue
{
    return m_vulkan_graphics_queue;
}

auto Device_impl::get_present_queue() const -> VkQueue
{
    return m_vulkan_present_queue;
}

auto Device_impl::get_capabilities() const -> const Capabilities&
{
    return m_capabilities;
}

auto Device_impl::get_driver_properties() const -> const VkPhysicalDeviceDriverProperties&
{
    return m_driver_properties;
}

auto Device_impl::get_memory_type(uint32_t memory_type_index) const -> const VkMemoryType&
{
    return m_memory_properties.memoryProperties.memoryTypes[memory_type_index];
}

auto Device_impl::get_memory_heap(uint32_t memory_heap_index) const -> const VkMemoryHeap&
{
    return m_memory_properties.memoryProperties.memoryHeaps[memory_heap_index];
}

auto Device_impl::get_immediate_commands() -> Vulkan_immediate_commands&
{
    return *m_immediate_commands.get();
}

auto Device_impl::get_handle(const Texture& texture, const Sampler& sampler) const -> uint64_t
{
    // TODO Implement bindless texture handles via descriptor indexing
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Device_impl::create_dummy_texture(const erhe::dataformat::Format format) -> std::shared_ptr<Texture>
{
    // TODO Move function to Device instead of Device_impl?
    const Texture_create_info create_info{
        .device      = m_device,
        .usage_mask  = Image_usage_flag_bit_mask::sampled | Image_usage_flag_bit_mask::transfer_dst,
        .type        = Texture_type::texture_2d,
        .pixelformat = format,
        .width       = 2,
        .height      = 2,
        .debug_label = "dummy"
    };

    auto texture = std::make_shared<Texture>(m_device, create_info);

    const std::size_t bytes_per_pixel   = erhe::dataformat::get_format_size_bytes(format);
    const std::size_t width             = 2;
    const std::size_t height            = 2;
    const std::size_t src_bytes_per_row = width * bytes_per_pixel;
    const std::size_t byte_count        = height * src_bytes_per_row;

    // Fill with a simple pattern -- content doesn't matter much for a dummy texture
    std::vector<uint8_t> dummy_pixels(byte_count, 0);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::size_t offset = (y * width + x) * bytes_per_pixel;
            // Fill first 4 bytes with a visible pattern, rest with 0
            std::size_t fill = std::min(bytes_per_pixel, std::size_t{4});
            const uint8_t pattern[4] = {
                static_cast<uint8_t>(((x + y) & 1) ? 0xee : 0xcc),
                0x11,
                static_cast<uint8_t>(((x + y) & 1) ? 0xdd : 0xbb),
                0xff
            };
            memcpy(&dummy_pixels[offset], pattern, fill);
        }
    }

    Ring_buffer_client   texture_upload_buffer{m_device, erhe::graphics::Buffer_target::transfer_src, "dummy texture upload"};
    Ring_buffer_range    buffer_range = texture_upload_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, byte_count);
    std::span<std::byte> dst_span     = buffer_range.get_span();
    memcpy(dst_span.data(), dummy_pixels.data(), byte_count);
    buffer_range.bytes_written(byte_count);
    buffer_range.close();

    const std::size_t src_bytes_per_image = height * src_bytes_per_row;
    Blit_command_encoder encoder{m_device};
    encoder.copy_from_buffer(
        buffer_range.get_buffer()->get_buffer(),         // source_buffer
        buffer_range.get_byte_start_offset_in_buffer(),  // source_offset
        src_bytes_per_row,                               // source_bytes_per_row
        src_bytes_per_image,                             // source_bytes_per_image
        glm::ivec3{2, 2, 1},                             // source_size
        texture.get(),                                   // destination_texture
        0,                                               // destination_slice
        0,                                               // destination_level
        glm::ivec3{0, 0, 0}                              // destination_origin
    );

    buffer_range.release();

    return texture;
}

auto Device_impl::get_shader_monitor() -> Shader_monitor&
{
    return m_shader_monitor;
}

auto Device_impl::get_info() const -> const Device_info&
{
    return m_info;
}

auto Device_impl::get_graphics_config() const -> const Graphics_config&
{
    return m_graphics_config;
}

auto Device_impl::get_allocator() -> VmaAllocator&
{
    return m_vma_allocator;
}

auto Device_impl::get_buffer_alignment(const Buffer_target target) -> std::size_t
{
    switch (target) {
        case Buffer_target::storage: {
            return m_info.shader_storage_buffer_offset_alignment;
        }

        case Buffer_target::uniform: {
            return m_info.uniform_buffer_offset_alignment;
        }

        case Buffer_target::draw_indirect: {
            // TODO Consider Draw_primitives_indirect_command
            return sizeof(Draw_indexed_primitives_indirect_command);
        }
        default: {
            return 64; // TODO
        }
    }
}

void Device_impl::upload_to_buffer(const Buffer& buffer, const size_t offset, const void* data, const size_t length)
{
    // For host-visible buffers, map and copy directly
    // const_cast is needed because map_bytes is non-const (it modifies internal map state)
    Buffer_impl& impl = const_cast<Buffer_impl&>(buffer.get_impl());
    if (impl.is_host_visible()) {
        std::span<std::byte> map = impl.map_bytes(offset, length);
        if (!map.empty()) {
            std::memcpy(map.data(), data, length);
            impl.end_write(offset, length);
            return;
        }
    }

    // Staging buffer upload for non-host-visible (device-local) buffers
    VkBufferCreateInfo staging_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size  = length,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VmaAllocationCreateInfo staging_alloc_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_allocation_info{};
    VkResult result = vmaCreateBuffer(m_vma_allocator, &staging_buffer_create_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info);
    if (result != VK_SUCCESS) {
        log_buffer->error("upload_to_buffer: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(m_vma_allocator, staging_allocation, "upload_to_buffer staging");
    std::memcpy(staging_allocation_info.pMappedData, data, length);
    vmaFlushAllocation(m_vma_allocator, staging_allocation, 0, length);

    const VkBufferCopy copy_region{
        .srcOffset = 0,
        .dstOffset = offset,
        .size      = length
    };

    // Chain the transfer write to the precise set of consumer stages the
    // buffer was created for (derived from its Buffer_usage mask). This
    // prevents RAW hazards against downstream readers that don't emit their
    // own pre-barrier. transfer_dst in the usage mask also covers the
    // cross-frame WAW against the next vkCmdCopyBuffer on this buffer.
    // Range-scoped so unrelated work isn't over-synchronized.
    const Vk_stage_access_2 dst_scope = buffer_usage_to_vk_stage_access(impl.get_usage());
    const VkPipelineStageFlags2 dst_stage_mask =
        (dst_scope.stage != 0) ? dst_scope.stage : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    const VkAccessFlags2 dst_access_mask =
        (dst_scope.access != 0)
            ? dst_scope.access
            : (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkBufferMemoryBarrier2 buffer_barrier{
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = impl.get_vk_buffer(),
        .offset              = static_cast<VkDeviceSize>(offset),
        .size                = static_cast<VkDeviceSize>(length)
    };
    const VkDependencyInfo post_copy_dep_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &buffer_barrier,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };

    VkCommandBuffer device_cb = get_device_frame_command_buffer();
    if (device_cb != VK_NULL_HANDLE) {
        // In-frame path: record into the active device cb and defer
        // staging destruction to the frame-completion handler. Serialize
        // against concurrent init-time callers.
        std::lock_guard<std::mutex> lock{m_recording_mutex};
        vkCmdCopyBuffer(device_cb, staging_buffer, impl.get_vk_buffer(), 1, &copy_region);
        vkCmdPipelineBarrier2(device_cb, &post_copy_dep_info);
        VmaAllocator allocator = m_vma_allocator;
        add_completion_handler(
            [allocator, staging_buffer, staging_allocation](Device_impl&) {
                vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
            }
        );
    } else {
        // No device frame active: fall back to immediate commands (blocks).
        log_buffer->warn(
            "upload_to_buffer: out-of-frame fallback path (blocks CPU until GPU "
            "finishes copy). buffer '{}' offset={} length={}",
            impl.get_debug_label().string_view(), offset, length
        );
        const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();
        vkCmdCopyBuffer(cmd.m_cmd_buf, staging_buffer, impl.get_vk_buffer(), 1, &copy_region);
        vkCmdPipelineBarrier2(cmd.m_cmd_buf, &post_copy_dep_info);
        Submit_handle submit = m_immediate_commands->submit(cmd);
        m_immediate_commands->wait(submit);

        vmaDestroyBuffer(m_vma_allocator, staging_buffer, staging_allocation);
    }
}

void Device_impl::upload_to_texture(
    const Texture&               texture,
    int                          level,
    int                          x,
    int                          y,
    int                          width,
    int                          height,
    erhe::dataformat::Format     pixelformat,
    const void*                  data,
    int                          row_stride
)
{
    if ((data == nullptr) || (width <= 0) || (height <= 0)) {
        return;
    }

    const std::size_t pixel_size  = erhe::dataformat::get_format_size_bytes(pixelformat);
    const std::size_t src_stride  = (row_stride > 0) ? static_cast<std::size_t>(row_stride) : (static_cast<std::size_t>(width) * pixel_size);
    const std::size_t data_size   = src_stride * static_cast<std::size_t>(height);

    // Create staging buffer
    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    const VkBufferCreateInfo staging_buffer_info{
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = data_size,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    VkBuffer       staging_buffer     = VK_NULL_HANDLE;
    VmaAllocation  staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo staging_alloc_result{};

    VkResult result = vmaCreateBuffer(m_vma_allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_alloc_result);
    if (result != VK_SUCCESS) {
        log_texture->error("upload_to_texture: staging buffer creation failed with {}", static_cast<int32_t>(result));
        return;
    }
    vmaSetAllocationName(m_vma_allocator, staging_allocation, "upload_to_texture staging");

    // Copy data to staging buffer
    std::memcpy(staging_alloc_result.pMappedData, data, data_size);
    vmaFlushAllocation(m_vma_allocator, staging_allocation, 0, data_size);

    const Texture_impl& tex_impl = texture.get_impl();
    VkImage destination_image = tex_impl.get_vk_image();

    const VkBufferImageCopy region{
        .bufferOffset      = 0,
        .bufferRowLength   = (row_stride > 0) ? static_cast<uint32_t>(row_stride / pixel_size) : 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = static_cast<uint32_t>(level),
            .baseArrayLayer = 0,
            .layerCount     = 1
        },
        .imageOffset = {x, y, 0},
        .imageExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        }
    };

    VkCommandBuffer device_cb = get_device_frame_command_buffer();
    if (device_cb != VK_NULL_HANDLE) {
        // In-frame path: record into the active device cb and defer
        // staging destruction to the frame-completion handler. Serialize
        // against concurrent init-time callers.
        std::lock_guard<std::mutex> lock{m_recording_mutex};
        tex_impl.transition_layout(device_cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(device_cb, staging_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        tex_impl.transition_layout(device_cb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VmaAllocator allocator = m_vma_allocator;
        add_completion_handler(
            [allocator, staging_buffer, staging_allocation](Device_impl&) {
                vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);
            }
        );
    } else {
        // No device frame active: fall back to immediate commands (blocks).
        log_texture->warn(
            "upload_to_texture: out-of-frame fallback path (blocks CPU until GPU "
            "finishes copy). texture '{}' level={} {}x{}",
            tex_impl.get_debug_label().string_view(), level, width, height
        );
        const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();

        tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd.m_cmd_buf, staging_buffer, destination_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        tex_impl.transition_layout(cmd.m_cmd_buf, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Submit and wait (staging buffer must survive until copy completes)
        Submit_handle submit = m_immediate_commands->submit(cmd);
        m_immediate_commands->wait(submit);

        vmaDestroyBuffer(m_vma_allocator, staging_buffer, staging_allocation);
    }
}

void Device_impl::add_completion_handler(std::function<void(Device_impl&)> callback)
{
    m_completion_handlers.emplace_back(m_frame_index, std::move(callback));
}

void Device_impl::on_thread_enter()
{
}

void Device_impl::frame_completed(const uint64_t completed_frame)
{
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(completed_frame);
    }
    for (const Completion_handler& entry : m_completion_handlers) {
        if (entry.frame_number == completed_frame) {
            entry.callback(*this);
        }
    }
    auto i = std::remove_if(
        m_completion_handlers.begin(),
        m_completion_handlers.end(),
        [completed_frame](Completion_handler& entry) { return entry.frame_number == completed_frame; }
    );
    if (i != m_completion_handlers.end()) {
        m_completion_handlers.erase(i, m_completion_handlers.end());
    }
}

auto Device_impl::wait_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::idle);
    m_state = Device_frame_state::waited;
    return true;
}

auto Device_impl::wait_swapchain_frame(Frame_state& out_frame_state) -> bool
{
    // Must be called after wait_frame() (device state = waited). The Vulkan
    // swapchain owns its own state machine driven by wait/begin/end; skipping
    // this call is valid when the desktop window is not being rendered into
    // (e.g. OpenXR), provided begin_swapchain_frame / end_swapchain_frame are
    // skipped to match.
    ERHE_VERIFY(m_state == Device_frame_state::waited);

    bool result = false;
    if (m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            result = swapchain->get_impl().wait_frame(out_frame_state);
        }
    }
    if (!result) {
        out_frame_state.predicted_display_time   = 0;
        out_frame_state.predicted_display_period = 0;
        out_frame_state.should_render            = false;
        return false;
    }
    return true;
}

auto Device_impl::begin_frame() -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::waited);
    ERHE_VULKAN_SYNC_TRACE("[FRAME_BEGIN] frame_index={}", m_frame_index);

    // Open a device frame for recording with no swapchain involvement.
    // Reset the per-frame descriptor pool, open the slot's primary
    // command buffer for recording, and flip state. The caller may then
    // nest a swapchain frame via begin_swapchain_frame.
    reset_descriptor_pool();
    m_had_swapchain_frame = false;

    // Open the slot's device command buffer. ensure_device_frame_command_buffer
    // was run by wait_frame (via Swapchain_impl::setup_frame) when a swapchain
    // is present; for no-swapchain device frames the cb is VK_NULL_HANDLE and
    // we leave the body empty (no Vulkan call).
    const size_t slot = static_cast<size_t>(get_frame_in_flight_index());
    Device_frame_in_flight& df = m_device_submit_history[slot];
    if (df.command_buffer != VK_NULL_HANDLE) {
        const VkCommandBufferBeginInfo begin_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        const VkResult result = vkBeginCommandBuffer(df.command_buffer, &begin_info);
        if (result != VK_SUCCESS) {
            log_context->critical(
                "vkBeginCommandBuffer() failed with {} {}",
                static_cast<int32_t>(result), c_str(result)
            );
            abort();
        }
    }

    m_active_device_frame_command_buffer = df.command_buffer;
    m_state = Device_frame_state::recording;
    return true;
}

auto Device_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    // Compat path: combine device-frame open and swapchain-frame open.
    const bool device_ok = begin_frame();
    if (!device_ok) {
        return false;
    }
    Frame_state dummy_state{};
    return begin_swapchain_frame(frame_begin_info, dummy_state);
}

auto Device_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    // Under the unified command-buffer model, render passes record
    // directly into the device-frame cb via acquire_shared_command_buffer.
    // Everything (offscreen render passes, swapchain render pass,
    // transfers, clears, blits) lands in the single device-cb submit
    // below.

    // Valid entry states: in_swapchain_frame (old editor path that went
    // begin_frame(info) -> render -> end_frame(info)), or recording (new
    // path that already called end_swapchain_frame).
    ERHE_VERIFY(
        (m_state == Device_frame_state::in_swapchain_frame) ||
        (m_state == Device_frame_state::recording)
    );

    // Compat: caller did not call end_swapchain_frame first. Fold it in
    // so the nested swapchain frame is properly closed before submit.
    if (m_state == Device_frame_state::in_swapchain_frame) {
        end_swapchain_frame(frame_end_info);
    }

    // Close the device command buffer we opened in begin_frame(). The
    // subsequent submit path differs depending on whether a swapchain
    // frame was nested: Swapchain_impl::end_frame submits with acquire
    // wait + present signal + fence; our own no-swapchain branch submits
    // with just the fence so the slot is still paced.
    const size_t slot = static_cast<size_t>(get_frame_in_flight_index());
    Device_frame_in_flight& df = m_device_submit_history[slot];
    if (df.command_buffer != VK_NULL_HANDLE) {
        const VkResult r = vkEndCommandBuffer(df.command_buffer);
        if (r != VK_SUCCESS) {
            log_context->critical(
                "vkEndCommandBuffer() failed with {} {}",
                static_cast<int32_t>(r), c_str(r)
            );
            abort();
        }
    }
    m_active_device_frame_command_buffer = VK_NULL_HANDLE;

    bool result = true;
    if (m_had_swapchain_frame && m_surface) {
        Swapchain* swapchain = m_surface->get_swapchain();
        if (swapchain != nullptr) {
            result = swapchain->get_impl().end_frame(frame_end_info);
        }
    } else if ((df.command_buffer != VK_NULL_HANDLE) && (df.submit_fence != VK_NULL_HANDLE)) {
        // Device-only frame (no swapchain nested): submit the cb so the
        // slot's fence gets signaled when the GPU completes, enabling
        // the next wait_frame on this slot to recycle resources.
        const VkCommandBufferSubmitInfo cb_info{
            .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext         = nullptr,
            .commandBuffer = df.command_buffer,
            .deviceMask    = 0,
        };
        const VkSubmitInfo2 submit_info{
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext                    = nullptr,
            .flags                    = 0,
            .waitSemaphoreInfoCount   = 0,
            .pWaitSemaphoreInfos      = nullptr,
            .commandBufferInfoCount   = 1,
            .pCommandBufferInfos      = &cb_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos    = nullptr,
        };
        const VkResult r = vkQueueSubmit2(m_vulkan_graphics_queue, 1, &submit_info, df.submit_fence);
        if (r != VK_SUCCESS) {
            log_context->critical(
                "vkQueueSubmit2() for device-only frame failed with {} {}",
                static_cast<int32_t>(r), c_str(r)
            );
            abort();
        }
    }
    m_had_swapchain_frame = false;

    update_frame_completion();

    ERHE_VULKAN_SYNC_TRACE("[FRAME_END] frame_index={}", m_frame_index);
    m_state = Device_frame_state::idle;
    return result;
}

auto Device_impl::end_frame() -> bool
{
    // No-args end_frame: caller used the new split (end_swapchain_frame was
    // called if a swapchain frame was nested). Frame_end_info is discarded
    // by every backend's Swapchain::end_frame anyway.
    return end_frame(Frame_end_info{});
}

auto Device_impl::begin_swapchain_frame(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_state == Device_frame_state::recording);

    if (!m_surface) {
        out_frame_state.should_render = false;
        return false;
    }
    Swapchain* swapchain = m_surface->get_swapchain();
    if (swapchain == nullptr) {
        out_frame_state.should_render = false;
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(frame_begin_info);
    out_frame_state.should_render = ok;
    if (ok) {
        m_state = Device_frame_state::in_swapchain_frame;
        m_had_swapchain_frame = true;
    }
    return ok;
}

void Device_impl::end_swapchain_frame(const Frame_end_info& /*frame_end_info*/)
{
    ERHE_VERIFY(m_state == Device_frame_state::in_swapchain_frame);
    // No submit yet; end_frame() performs the actual vkQueueSubmit2 +
    // vkQueuePresentKHR (guarded on m_had_swapchain_frame). We just flip
    // state back to recording so end_frame's precondition is met.
    m_state = Device_frame_state::recording;
}

void Device_impl::wait_idle()
{
    const VkResult result = vkDeviceWaitIdle(m_vulkan_device);
    if (result != VK_SUCCESS) {
        log_context->error(
            "vkDeviceWaitIdle() failed with {} {}",
            static_cast<int32_t>(result), c_str(result)
        );
    }

    // Every frame submitted so far is guaranteed complete on the GPU.
    // Drive frame_completed() up to the last submitted frame so ring
    // buffers recycle and completion handlers fire.
    //
    // m_frame_index is the next frame to be submitted; frames strictly
    // less than m_frame_index have already called update_frame_completion()
    // (which does ++m_frame_index). update_frame_completion() also advances
    // m_latest_completed_frame as the GPU timeline semaphore reports
    // completion; here we just force-complete everything up to the latest
    // submitted.
    if (m_frame_index > 0) {
        const uint64_t last_submitted = m_frame_index - 1;
        for (; m_latest_completed_frame <= last_submitted; ++m_latest_completed_frame) {
            frame_completed(m_latest_completed_frame);
        }
    }

    // Anything left in m_completion_handlers is for a frame that never
    // submitted - drain it too so staging buffers etc. are released.
    for (const Completion_handler& entry : m_completion_handlers) {
        entry.callback(*this);
    }
    m_completion_handlers.clear();
}

auto Device_impl::is_in_device_frame() const -> bool
{
    return (m_state == Device_frame_state::recording) ||
           (m_state == Device_frame_state::in_swapchain_frame);
}

auto Device_impl::is_in_swapchain_frame() const -> bool
{
    return m_state == Device_frame_state::in_swapchain_frame;
}

void Device_impl::update_frame_completion()
{
    VkResult result = VK_SUCCESS;

    const VkSemaphoreSubmitInfo signal_semaphore_info{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = m_vulkan_frame_end_semaphore,
        .value       = m_frame_index,
        .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0,
    };
    const VkSubmitInfo2 submit_info{
        .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext                    = nullptr,
        .flags                    = 0,
        .waitSemaphoreInfoCount   = 0,
        .pWaitSemaphoreInfos      = nullptr,
        .commandBufferInfoCount   = 0,
        .pCommandBufferInfos      = nullptr,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &signal_semaphore_info,
    };
    log_context->trace("vkQueueSubmit2() end of frame timeline semaphore @ frame index = {}", m_frame_index);
    result = vkQueueSubmit2(m_vulkan_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        log_context->critical("vkQueueSubmit2() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    ++m_frame_index;

    uint64_t latest_completed_frame{0};
    result = vkGetSemaphoreCounterValue(m_vulkan_device, m_vulkan_frame_end_semaphore, &latest_completed_frame);
    if (result != VK_SUCCESS) {
        log_context->error("vkGetSemaphoreCounterValue() failed with {} {}", static_cast<int32_t>(result), c_str(result));
    } else {
        for (; m_latest_completed_frame <= latest_completed_frame; ++m_latest_completed_frame) {
            log_context->trace("GPU has completed frame index = {}", m_latest_completed_frame);
            frame_completed(m_latest_completed_frame);
        }
    }
}

auto Device_impl::allocate_ring_buffer_entry(
    Buffer_target     buffer_target,
    Ring_buffer_usage ring_buffer_usage,
    std::size_t       byte_count
) -> Ring_buffer_range
{
    m_need_sync = true;
    const std::size_t required_alignment = erhe::utility::next_power_of_two_16bit(get_buffer_alignment(buffer_target));
    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(ring_buffer_usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, ring_buffer_usage, byte_count);
        }
    }

    // No existing usable buffer found, create new buffer
    Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, 4 * byte_count),
        .ring_buffer_usage = ring_buffer_usage,
        .debug_label       = "Ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    return m_ring_buffers.back()->acquire(required_alignment, ring_buffer_usage, byte_count);
}

void Device_impl::memory_barrier(Memory_barrier_mask barriers)
{
    // Record a global memory barrier on the active device-frame cb.
    // OpenGL's glMemoryBarrier semantics: "make all prior GPU writes
    // visible to the specified dst uses". We translate the mask bits to
    // Vulkan dst stage/access and use a conservative src that covers
    // any prior memory write.
    //
    // get_device_frame_command_buffer() returns VK_NULL_HANDLE when
    // either no device frame is active or a render pass is currently
    // recording. vkCmdPipelineBarrier2 inside a render pass without a
    // matching subpass self-dependency is invalid; callers must emit
    // global memory barriers outside render passes.
    VkCommandBuffer cb = get_device_frame_command_buffer();
    if (cb == VK_NULL_HANDLE) {
        if (is_in_device_frame()) {
            m_device.device_message(
                Message_severity::error,
                "memory_barrier() called inside a render pass; dropped. "
                "Global memory barriers must be emitted outside render passes "
                "because our render passes do not declare matching subpass "
                "self-dependencies."
            );
        }
        return;
    }

    const unsigned int mask = static_cast<unsigned int>(barriers);

    VkPipelineStageFlags2 dst_stage  = 0;
    VkAccessFlags2        dst_access = 0;

    constexpr unsigned int vertex_attrib_array_barrier_bit  = 0x00000001u;
    constexpr unsigned int element_array_barrier_bit        = 0x00000002u;
    constexpr unsigned int uniform_barrier_bit              = 0x00000004u;
    constexpr unsigned int texture_fetch_barrier_bit        = 0x00000008u;
    constexpr unsigned int shader_image_access_barrier_bit  = 0x00000020u;
    constexpr unsigned int command_barrier_bit              = 0x00000040u;
    constexpr unsigned int pixel_buffer_barrier_bit         = 0x00000080u;
    constexpr unsigned int texture_update_barrier_bit       = 0x00000100u;
    constexpr unsigned int buffer_update_barrier_bit        = 0x00000200u;
    constexpr unsigned int framebuffer_barrier_bit          = 0x00000400u;
    constexpr unsigned int atomic_counter_barrier_bit       = 0x00001000u;
    constexpr unsigned int shader_storage_barrier_bit       = 0x00002000u;
    constexpr unsigned int client_mapped_buffer_barrier_bit = 0x00004000u;

    constexpr VkPipelineStageFlags2 any_shader_stage =
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    constexpr VkAccessFlags2        shader_storage_rw =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;

    if ((mask & vertex_attrib_array_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        dst_access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if ((mask & element_array_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        dst_access |= VK_ACCESS_2_INDEX_READ_BIT;
    }
    if ((mask & uniform_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= VK_ACCESS_2_UNIFORM_READ_BIT;
    }
    if ((mask & texture_fetch_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }
    if ((mask & shader_image_access_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & command_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        dst_access |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    }
    if ((mask & pixel_buffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & texture_update_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & buffer_update_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        dst_access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    }
    if ((mask & framebuffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
                   |  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                   |  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        dst_access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                   |  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                   |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                   |  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if ((mask & atomic_counter_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & shader_storage_barrier_bit) != 0) {
        dst_stage  |= any_shader_stage;
        dst_access |= shader_storage_rw;
    }
    if ((mask & client_mapped_buffer_barrier_bit) != 0) {
        dst_stage  |= VK_PIPELINE_STAGE_2_HOST_BIT;
        dst_access |= VK_ACCESS_2_HOST_READ_BIT;
    }

    if (dst_stage == 0) {
        return;
    }

    const VkMemoryBarrier2 memory_barrier_info{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask  = dst_stage,
        .dstAccessMask = dst_access,
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &memory_barrier_info,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(cb, &dependency_info);

    ERHE_VULKAN_SYNC_TRACE(
        "[MEM_BARRIER] mask=0x{:x} src_stage={} src_access={} dst_stage={} dst_access={}",
        mask,
        pipeline_stage_flags_str(memory_barrier_info.srcStageMask),
        access_flags_str(memory_barrier_info.srcAccessMask),
        pipeline_stage_flags_str(memory_barrier_info.dstStageMask),
        access_flags_str(memory_barrier_info.dstAccessMask)
    );
}

auto Device_impl::get_format_properties(erhe::dataformat::Format format) const -> Format_properties
{
    //ERHE_FATAL("Not implemented");
    const VkFormat vulkan_format = to_vulkan(format);
    VkFormatProperties2 vulkan_properties{
        .sType            = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext            = nullptr,
        .formatProperties = {}
    };
    vkGetPhysicalDeviceFormatProperties2(m_vulkan_physical_device, vulkan_format, &vulkan_properties);
    //const VkFormatFeatureFlags supported =
    //    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
    using namespace erhe::utility;
    const uint32_t features = vulkan_properties.formatProperties.optimalTilingFeatures;

    Format_properties result{
        .supported          = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)),
        .color_renderable   = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)),
        .depth_renderable   = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) && (erhe::dataformat::get_depth_size_bits(format) > 0),
        .stencil_renderable = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) && (erhe::dataformat::get_stencil_size_bits(format) > 0),
        .filter             = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)),
        .framebuffer_blend  = erhe::utility::test_bit_set(features, static_cast<uint32_t>(VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)),
        .red_size           = static_cast<int>(erhe::dataformat::get_red_size_bits    (format)),
        .green_size         = static_cast<int>(erhe::dataformat::get_green_size_bits  (format)),
        .blue_size          = static_cast<int>(erhe::dataformat::get_blue_size_bits   (format)),
        .alpha_size         = static_cast<int>(erhe::dataformat::get_alpha_size_bits  (format)),
        .depth_size         = static_cast<int>(erhe::dataformat::get_depth_size_bits  (format)),
        .stencil_size       = static_cast<int>(erhe::dataformat::get_stencil_size_bits(format))
    };

    // Query image format properties for 2D array limits and sample counts.
    // Only probe with vkGetPhysicalDeviceImageFormatProperties2 if the format
    // actually supports the requested usage according to the format features
    // we just queried -- otherwise the call returns VK_ERROR_FORMAT_NOT_SUPPORTED
    // and the validation layer flags it as a best-practices error.
    const bool is_depth = erhe::dataformat::get_depth_size_bits(format) > 0;
    const VkImageUsageFlags usage = is_depth
        ? (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    const bool attachment_supported = is_depth
        ? (result.depth_renderable || result.stencil_renderable)
        : result.color_renderable;
    if (!result.supported || !attachment_supported) {
        return result;
    }

    const VkPhysicalDeviceImageFormatInfo2 image_format_info{
        .sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext  = nullptr,
        .format = vulkan_format,
        .type   = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage  = usage,
        .flags  = 0
    };

    VkImageFormatProperties2 image_format_properties{
        .sType             = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext             = nullptr,
        .imageFormatProperties = {}
    };

    VkResult image_result = vkGetPhysicalDeviceImageFormatProperties2(
        m_vulkan_physical_device, &image_format_info, &image_format_properties
    );
    if (image_result == VK_SUCCESS) {
        const VkImageFormatProperties& p = image_format_properties.imageFormatProperties;
        result.texture_2d_array_max_width  = static_cast<int>(p.maxExtent.width);
        result.texture_2d_array_max_height = static_cast<int>(p.maxExtent.height);
        result.texture_2d_array_max_layers = static_cast<int>(p.maxArrayLayers);

        for (int bit = 0; bit < 7; ++bit) {
            if (p.sampleCounts & (1u << bit)) {
                result.texture_2d_sample_counts.push_back(1 << bit);
            }
        }
    }

    return result;
}

auto Device_impl::probe_image_format_support(
    const erhe::dataformat::Format format,
    const uint64_t                 usage_mask
) const -> bool
{
    const VkFormat          vk_format = to_vulkan(format);
    const VkImageUsageFlags vk_usage  = get_vulkan_image_usage_flags(usage_mask);
    if ((vk_format == VK_FORMAT_UNDEFINED) || (vk_usage == 0)) {
        return false;
    }

    const VkPhysicalDeviceImageFormatInfo2 image_format_info{
        .sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext  = nullptr,
        .format = vk_format,
        .type   = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage  = vk_usage,
        .flags  = 0
    };

    VkImageFormatProperties2 image_format_properties{
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext                 = nullptr,
        .imageFormatProperties = {}
    };

    const VkResult image_result = vkGetPhysicalDeviceImageFormatProperties2(
        m_vulkan_physical_device, &image_format_info, &image_format_properties
    );
    return image_result == VK_SUCCESS;
}

auto Device_impl::get_supported_depth_stencil_formats() const -> std::vector<erhe::dataformat::Format>
{
    std::vector<erhe::dataformat::Format> result;
    // Query common depth/stencil formats for support
    const std::vector<erhe::dataformat::Format> candidates = {
        erhe::dataformat::Format::format_d32_sfloat,
        erhe::dataformat::Format::format_d32_sfloat_s8_uint,
        erhe::dataformat::Format::format_d24_unorm_s8_uint,
        erhe::dataformat::Format::format_d16_unorm,
    };
    for (erhe::dataformat::Format candidate : candidates) {
        Format_properties properties = get_format_properties(candidate);
        if (properties.depth_renderable || properties.stencil_renderable) {
            result.push_back(candidate);
        }
    }
    return result;
}

void Device_impl::sort_depth_stencil_formats(std::vector<erhe::dataformat::Format>& formats, unsigned int sort_flags, int requested_sample_count) const
{
    // Simple sort: prefer depth+stencil over depth-only, prefer higher precision
    static_cast<void>(sort_flags);
    static_cast<void>(requested_sample_count);
    std::sort(formats.begin(), formats.end(), [](erhe::dataformat::Format a, erhe::dataformat::Format b) {
        int a_depth   = static_cast<int>(erhe::dataformat::get_depth_size_bits(a));
        int b_depth   = static_cast<int>(erhe::dataformat::get_depth_size_bits(b));
        int a_stencil = static_cast<int>(erhe::dataformat::get_stencil_size_bits(a));
        int b_stencil = static_cast<int>(erhe::dataformat::get_stencil_size_bits(b));
        int a_score   = a_depth + a_stencil;
        int b_score   = b_depth + b_stencil;
        return a_score > b_score;
    });
}

auto Device_impl::choose_depth_stencil_format(const std::vector<erhe::dataformat::Format>& formats) const -> erhe::dataformat::Format
{
    if (formats.empty()) {
        return erhe::dataformat::Format::format_d32_sfloat;
    }
    return formats.front();
}

auto Device_impl::choose_depth_stencil_format(unsigned int sort_flags, int requested_sample_count) const -> erhe::dataformat::Format
{
    std::vector<erhe::dataformat::Format> formats = get_supported_depth_stencil_formats();
    sort_depth_stencil_formats(formats, sort_flags, requested_sample_count);
    return choose_depth_stencil_format(formats);
}

void Device_impl::clear_texture(const Texture& texture, std::array<double, 4> value)
{
    const Texture_impl& tex_impl = texture.get_impl();
    VkImage image = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    const erhe::dataformat::Format pixelformat = tex_impl.get_pixelformat();
    const bool is_depth   = erhe::dataformat::get_depth_size_bits(pixelformat) > 0;
    const bool is_stencil = erhe::dataformat::get_stencil_size_bits(pixelformat) > 0;

    const VkImageLayout final_layout = (is_depth || is_stencil)
        ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    auto record = [&](VkCommandBuffer cb) {
        tex_impl.transition_layout(cb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        if (is_depth || is_stencil) {
            const VkClearDepthStencilValue clear_depth_stencil{
                .depth   = static_cast<float>(value[0]),
                .stencil = static_cast<uint32_t>(value[1])
            };
            VkImageAspectFlags aspect_mask = 0;
            if (is_depth)   aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            if (is_stencil) aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            const VkImageSubresourceRange range{aspect_mask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
            vkCmdClearDepthStencilImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &range);
        } else {
            const VkClearColorValue clear_color{
                .float32 = {
                    static_cast<float>(value[0]),
                    static_cast<float>(value[1]),
                    static_cast<float>(value[2]),
                    static_cast<float>(value[3])
                }
            };
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
            vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
        }
        tex_impl.transition_layout(cb, final_layout);
    };

    VkCommandBuffer device_cb = get_device_frame_command_buffer();
    if (device_cb != VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock{m_recording_mutex};
        record(device_cb);
    } else {
        const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();
        record(cmd.m_cmd_buf);
        Submit_handle submit = m_immediate_commands->submit(cmd);
        m_immediate_commands->wait(submit);
    }
}

// to_vk_image_layout is now in vulkan_helpers.cpp/hpp

void Device_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    const Texture_impl& tex_impl = texture.get_impl();
    VkImage image = tex_impl.get_vk_image();
    if (image == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBuffer device_cb = get_device_frame_command_buffer();
    if (device_cb != VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock{m_recording_mutex};
        tex_impl.transition_layout(device_cb, to_vk_image_layout(new_layout));
    } else {
        const Vulkan_immediate_commands::Command_buffer_wrapper& cmd = m_immediate_commands->acquire();
        tex_impl.transition_layout(cmd.m_cmd_buf, to_vk_image_layout(new_layout));
        Submit_handle submit = m_immediate_commands->submit(cmd);
        m_immediate_commands->wait(submit);
    }
}

void Device_impl::cmd_texture_barrier(uint64_t usage_before, uint64_t usage_after)
{
    // Strip the user_synchronized modifier -- it only controls whether
    // the automatic end-of-renderpass barrier fires; the actual
    // stage/access mapping should ignore it.
    const uint64_t src_usage = usage_before & ~Image_usage_flag_bit_mask::user_synchronized;
    const uint64_t dst_usage = usage_after  & ~Image_usage_flag_bit_mask::user_synchronized;

    const Vk_stage_access src_sa = usage_to_vk_stage_access(src_usage, false);
    const Vk_stage_access dst_sa = usage_to_vk_stage_access(dst_usage, false);

    if ((src_sa.stage == 0) || (dst_sa.stage == 0)) {
        return;
    }

    VkCommandBuffer command_buffer = acquire_shared_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        log_render_pass->error("cmd_texture_barrier(): no device-frame command buffer available");
        return;
    }

    const VkMemoryBarrier2 memory_barrier{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = static_cast<VkPipelineStageFlags2>(src_sa.stage),
        .srcAccessMask = static_cast<VkAccessFlags2>(src_sa.access),
        .dstStageMask  = static_cast<VkPipelineStageFlags2>(dst_sa.stage),
        .dstAccessMask = static_cast<VkAccessFlags2>(dst_sa.access),
    };
    const VkDependencyInfo dependency_info{
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &memory_barrier,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency_info);

    ERHE_VULKAN_SYNC_TRACE(
        "[EXPLICIT_BARRIER] src_stage={} src_access={} dst_stage={} dst_access={}",
        pipeline_stage_flags_str(memory_barrier.srcStageMask),
        access_flags_str(memory_barrier.srcAccessMask),
        pipeline_stage_flags_str(memory_barrier.dstStageMask),
        access_flags_str(memory_barrier.dstAccessMask)
    );
}

void Device_impl::start_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vulkan_instance);
    api->StartFrameCapture(device, nullptr);
    log_context->info("RenderDoc: StartFrameCapture()");
}

void Device_impl::end_frame_capture()
{
    RENDERDOC_API_1_7_0* api = static_cast<RENDERDOC_API_1_7_0*>(erhe::window::get_renderdoc_api());
    if (api == nullptr) {
        return;
    }
    RENDERDOC_DevicePointer device = RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vulkan_instance);
    uint32_t result = api->EndFrameCapture(device, nullptr);
    if (result == 0) {
        log_context->warn("RenderDoc: EndFrameCapture() failed");
        return;
    }
    if (api->IsTargetControlConnected()) {
        api->ShowReplayUI();
    } else {
        api->LaunchReplayUI(1, nullptr);
    }
}

auto Device_impl::make_blit_command_encoder() -> Blit_command_encoder
{
    return Blit_command_encoder(m_device);
}

auto Device_impl::make_compute_command_encoder() -> Compute_command_encoder
{
    return Compute_command_encoder(m_device);
}

auto Device_impl::make_render_command_encoder() -> Render_command_encoder
{
    return Render_command_encoder(m_device);
}

void Device_impl::set_debug_label(const VkObjectType object_type, const uint64_t object_handle, const char* label)
{
    if (!m_instance_extensions.m_VK_EXT_debug_utils) {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT name_info{
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext        = nullptr,
        .objectType   = object_type,
        .objectHandle = object_handle,
        .pObjectName  = label
    };
    VkResult result = vkSetDebugUtilsObjectNameEXT(m_vulkan_device, &name_info);
    if (result != VK_SUCCESS) {
        log_debug->warn(
            "vkSetDebugUtilsObjectNameEXT() failed with {} {}",
            static_cast<int32_t>(result),
            c_str(result)
        );
    }
}

void Device_impl::set_debug_label(const VkObjectType object_type, const uint64_t object_handle, const std::string& label)
{
    set_debug_label(object_type, object_handle, label.c_str());
}

// Active render pass tracking
Device_impl* Device_impl::s_device_impl{nullptr};

auto Device_impl::get_device_impl() -> Device_impl*
{
    return s_device_impl;
}

auto Device_impl::get_sync_pool() -> Device_sync_pool&
{
    return *m_sync_pool;
}

auto Device_impl::get_active_command_buffer() const -> VkCommandBuffer
{
    // The device-frame cb is the one thing every render pass, transfer, and
    // encoder records into under the unified command-buffer model. Return it
    // directly -- an active render pass shares the same cb, so there's no
    // need to route through m_active_render_pass.
    return m_active_device_frame_command_buffer;
}

auto Device_impl::get_device_frame_command_buffer() const -> VkCommandBuffer
{
    if (!is_in_device_frame()) {
        return VK_NULL_HANDLE;
    }
    // Transfer/blit/clear commands and their pipeline barriers are not
    // allowed inside a render pass instance (unless the render pass has
    // a subpass self-dependency, which ours don't). When a swapchain
    // render pass is mid-recording on the device cb, return VK_NULL_HANDLE
    // so callers (upload_to_*, clear_texture, blit encoder Recording_scope)
    // fall back to the immediate-commands path and record into an
    // independent cb.
    if (m_active_render_pass != nullptr) {
        return VK_NULL_HANDLE;
    }
    const size_t slot = static_cast<size_t>(get_frame_in_flight_index());
    return m_device_submit_history[slot].command_buffer;
}

auto Device_impl::get_active_render_pass() const -> VkRenderPass
{
    if (m_active_render_pass != nullptr) {
        return m_active_render_pass->m_render_pass;
    }
    return VK_NULL_HANDLE;
}

auto Device_impl::get_active_render_pass_impl() const -> Render_pass_impl*
{
    return m_active_render_pass;
}

void Device_impl::set_active_render_pass_impl(Render_pass_impl* render_pass_impl)
{
    m_active_render_pass = render_pass_impl;
}

auto Device_impl::get_number_of_frames_in_flight() const -> size_t
{
    return s_number_of_frames_in_flight;
}

auto Device_impl::get_frame_index() const -> uint64_t
{
    return m_frame_index;
}

auto Device_impl::get_frame_in_flight_index() const -> uint64_t
{
    return m_frame_index % get_number_of_frames_in_flight();
}

} // namespace erhe::graphics
