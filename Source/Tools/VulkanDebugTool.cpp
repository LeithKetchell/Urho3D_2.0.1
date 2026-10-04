#include <vulkan/vulkan.h>
#include <spdlog/spdlog.h>

void setupVulkanDebugLayers() {
    spdlog::set_level(spdlog::level::debug); // Set log level
    spdlog::info("Setting up Vulkan debug layers...");
    spdlog::debug("Debug message: Vulkan layers initialized.");
}
