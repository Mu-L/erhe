#include "erhe_graphics/device.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"

#include "volk.h"

namespace erhe::graphics {

bool Scoped_debug_group::s_enabled{false};

void Scoped_debug_group::begin()
{
    Device_impl* device_impl = Device_impl::get_device_impl();
    if (device_impl == nullptr) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {1.0f, 1.0f, 1.0f, 1.0f}
    };

    VkCommandBuffer command_buffer = device_impl->get_active_command_buffer();
    if (command_buffer != VK_NULL_HANDLE) {
        vkCmdBeginDebugUtilsLabelEXT(command_buffer, &label_info);
        return;
    }

    VkQueue queue = device_impl->get_graphics_queue();
    if (queue != VK_NULL_HANDLE) {
        vkQueueBeginDebugUtilsLabelEXT(queue, &label_info);
        m_used_queue = true;
    }
}

void Scoped_debug_group::end()
{
    Device_impl* device_impl = Device_impl::get_device_impl();
    if (device_impl == nullptr) {
        return;
    }

    if (m_used_queue) {
        VkQueue queue = device_impl->get_graphics_queue();
        if (queue != VK_NULL_HANDLE) {
            vkQueueEndDebugUtilsLabelEXT(queue);
        }
        return;
    }

    VkCommandBuffer command_buffer = device_impl->get_active_command_buffer();
    if (command_buffer != VK_NULL_HANDLE) {
        vkCmdEndDebugUtilsLabelEXT(command_buffer);
    }
}

} // namespace erhe::graphics
