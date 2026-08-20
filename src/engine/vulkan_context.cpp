/**
 * VulkanContext 实现
 */
#include "vulkan_context.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <cstring>

// ============================================================================
// 辅助：选择物理设备
// ============================================================================
static bool checkDeviceExtensionSupport(VkPhysicalDevice device,
                                        const std::vector<const char*>& exts)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(exts.begin(), exts.end());
    for (const auto& a : available)
        required.erase(a.extensionName);
    return required.empty();
}

static bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface,
                             const std::vector<const char*>& exts)
{
    // 队列家族
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, qf.data());

    bool graphics = false, present = false;
    for (uint32_t i = 0; i < qfCount; ++i)
    {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = true;
        VkBool32 p = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &p);
        if (p) present = true;
    }

    bool ext = checkDeviceExtensionSupport(device, exts);

    bool swapChainAdequate = false;
    if (ext)
    {
        uint32_t fc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &fc, nullptr);
        swapChainAdequate = fc > 0;
        uint32_t pc = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &pc, nullptr);
        swapChainAdequate = swapChainAdequate && pc > 0;
    }

    return graphics && present && ext && swapChainAdequate;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& av)
{
    for (const auto& f : av)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return av.empty() ? VkSurfaceFormatKHR{} : av[0];
}

static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& av)
{
    for (const auto& m : av)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* win)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    VkExtent2D e;
    e.width  = std::clamp<uint32_t>(static_cast<uint32_t>(w),
                                     caps.minImageExtent.width, caps.maxImageExtent.width);
    e.height = std::clamp<uint32_t>(static_cast<uint32_t>(h),
                                     caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanContext::VulkanContext(VkInstance instance, GLFWwindow* window,
                             uint32_t width, uint32_t height)
    : m_instance(instance), m_window(window), m_width(width), m_height(height)
{
    if (glfwCreateWindowSurface(instance, window, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface 失败");

    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();

    std::cout << "[VulkanContext] 渲染后端初始化完成。" << std::endl;
}

VulkanContext::~VulkanContext()
{
    if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);

    cleanupSwapChain();

    for (size_t i = 0; i < m_inFlightFences.size(); ++i)
    {
        vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_renderPass)  vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_device)      vkDestroyDevice(m_device, nullptr);
    if (m_surface)     vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    std::cout << "[VulkanContext] 已销毁。" << std::endl;
}

// ============================================================================
// 物理设备
// ============================================================================

void VulkanContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("未找到任何支持 Vulkan 的 GPU");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    static const std::vector<const char*> exts = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    for (const auto& d : devices)
    {
        if (isDeviceSuitable(d, m_surface, exts))
        {
            m_physicalDevice = d;
            VkPhysicalDeviceProperties p;
            vkGetPhysicalDeviceProperties(d, &p);
            std::cout << "[VulkanContext] 选择 GPU: " << p.deviceName << std::endl;
            return;
        }
    }
    throw std::runtime_error("未找到合适的 GPU");
}

void VulkanContext::createLogicalDevice()
{
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qf.data());

    uint32_t g = 0, p = 0;
    bool foundG = false, foundP = false;
    for (uint32_t i = 0; i < qfCount; ++i)
    {
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g = i; foundG = true; }
        VkBool32 ps = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &ps);
        if (ps) { p = i; foundP = true; }
        if (foundG && foundP) break;
    }
    if (!foundG || !foundP)
        throw std::runtime_error("未找到图形/呈现队列");

    std::set<uint32_t> unique = { g, p };
    std::vector<VkDeviceQueueCreateInfo> qci;
    float prio = 1.0f;
    for (auto qf : unique)
    {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = qf;
        qi.queueCount = 1;
        qi.pQueuePriorities = &prio;
        qci.push_back(qi);
    }

    VkPhysicalDeviceFeatures pdf{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &pdf);

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.pQueueCreateInfos = qci.data();
    ci.queueCreateInfoCount = static_cast<uint32_t>(qci.size());
    ci.pEnabledFeatures = &pdf;

    static const std::vector<const char*> exts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice 失败");

    vkGetDeviceQueue(m_device, g, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, p, 0, &m_presentQueue);
}

// ============================================================================
// 交换链
// ============================================================================

void VulkanContext::createSwapChain()
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fc, formats.data());

    uint32_t pc = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pc, nullptr);
    std::vector<VkPresentModeKHR> modes(pc);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pc, modes.data());

    VkSurfaceFormatKHR sf = chooseSwapSurfaceFormat(formats);
    VkPresentModeKHR   pm = chooseSwapPresentMode(modes);
    VkExtent2D         ex = chooseSwapExtent(caps, m_window);

    uint32_t ic = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && ic > caps.maxImageCount) ic = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = m_surface;
    ci.minImageCount = ic;
    ci.imageFormat = sf.format;
    ci.imageColorSpace = sf.colorSpace;
    ci.imageExtent = ex;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &ci, nullptr, &m_swapChain) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR 失败");

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &ic, nullptr);
    m_swapChainImages.resize(ic);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &ic, m_swapChainImages.data());

    m_swapChainImageFormat = sf.format;
    m_swapChainExtent = ex;
}

void VulkanContext::createImageViews()
{
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); ++i)
    {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = m_swapChainImages[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = m_swapChainImageFormat;
        ci.components = VkComponentMapping{};
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel = 0;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &ci, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView 失败");
    }
}

// ============================================================================
// 渲染通道
// ============================================================================

void VulkanContext::createRenderPass()
{
    VkAttachmentDescription ca{};
    ca.format = m_swapChainImageFormat;
    ca.samples = VK_SAMPLE_COUNT_1_BIT;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ca.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ca.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference caRef{};
    caRef.attachment = 0;
    caRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &caRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &ca;
    ci.subpassCount = 1;
    ci.pSubpasses = &sp;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    if (vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass 失败");
}

void VulkanContext::createFramebuffers()
{
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i)
    {
        VkImageView attachments[] = { m_swapChainImageViews[i] };
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = m_renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments = attachments;
        ci.width = m_swapChainExtent.width;
        ci.height = m_swapChainExtent.height;
        ci.layers = 1;
        if (vkCreateFramebuffer(m_device, &ci, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer 失败");
    }
}

// ============================================================================
// 命令池/缓冲
// ============================================================================

void VulkanContext::createCommandPool()
{
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qf.data());

    uint32_t g = 0;
    for (uint32_t i = 0; i < qfCount; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g = i; break; }

    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g;
    if (vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateCommandPool 失败");
}

void VulkanContext::createCommandBuffers()
{
    m_commandBuffers.resize(m_swapChainFramebuffers.size());
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());
    if (vkAllocateCommandBuffers(m_device, &ai, m_commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateCommandBuffers 失败");
}

// ============================================================================
// 同步对象
// ============================================================================

void VulkanContext::createSyncObjects()
{
    constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imagesInFlight.resize(m_swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fi, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
            throw std::runtime_error("创建同步对象失败");
    }
}

// ============================================================================
// 帧管理
// ============================================================================

uint32_t VulkanContext::beginFrame()
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapChain();
        return ~0u;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("vkAcquireNextImageKHR 失败");
    }

    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(m_device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

    vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &bi);

    VkRenderPassBeginInfo rbi{};
    rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass = m_renderPass;
    rbi.framebuffer = m_swapChainFramebuffers[imageIndex];
    rbi.renderArea.extent = m_swapChainExtent;
    VkClearValue cv{ { {0.05f, 0.05f, 0.08f, 1.0f} } };
    rbi.clearValueCount = 1;
    rbi.pClearValues = &cv;
    vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &rbi,
                         VK_SUBPASS_CONTENTS_INLINE);

    // 记录当前帧的 image index 供 endFrame 使用
    m_currentImageIndex = imageIndex;
    return imageIndex;
}

void VulkanContext::endFrame()
{
    vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);
    if (vkEndCommandBuffer(m_commandBuffers[m_currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("vkEndCommandBuffer 失败");

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSems[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = waitSems;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore sigSems[] = { m_renderFinishedSemaphores[m_currentFrame] };
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = sigSems;

    VkResult submitResult = vkQueueSubmit(m_graphicsQueue, 1, &si, m_inFlightFences[m_currentFrame]);
    if (submitResult != VK_SUCCESS)
    {
        std::cerr << "[VulkanContext] vkQueueSubmit 失败，VkResult=" << submitResult;
        if (submitResult == VK_ERROR_DEVICE_LOST)         std::cerr << " (DEVICE_LOST)";
        else if (submitResult == VK_ERROR_OUT_OF_HOST_MEMORY)   std::cerr << " (OUT_OF_HOST_MEMORY)";
        else if (submitResult == VK_ERROR_OUT_OF_DEVICE_MEMORY) std::cerr << " (OUT_OF_DEVICE_MEMORY)";
        std::cerr << std::endl;
        throw std::runtime_error("vkQueueSubmit 失败");
    }

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = sigSems;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapChain;
    pi.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_presentQueue, &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("vkQueuePresentKHR 失败");
    }

    m_currentFrame = (m_currentFrame + 1) % m_inFlightFences.size();
}

VkCommandBuffer VulkanContext::commandBuffer() const
{
    return m_commandBuffers[m_currentFrame];
}

// ============================================================================
// 交换链重建
// ============================================================================

void VulkanContext::cleanupSwapChain()
{
    for (auto fb : m_swapChainFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto iv : m_swapChainImageViews) vkDestroyImageView(m_device, iv, nullptr);
    if (m_swapChain) vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChainFramebuffers.clear();
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();
}

void VulkanContext::recreateSwapChain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    while (w == 0 || h == 0)
    {
        glfwGetFramebufferSize(m_window, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(m_device);

    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createFramebuffers();

    // 重新分配命令缓冲以匹配新的帧缓冲数量
    // vkDeviceWaitIdle 后 GPU 已空闲，可安全释放旧命令缓冲
    if (!m_commandBuffers.empty())
    {
        vkFreeCommandBuffers(m_device, m_commandPool,
            static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
        m_commandBuffers.clear();
    }
    createCommandBuffers();

    // 重新调整 m_imagesInFlight 大小以匹配新的交换链图像数
    // 旧 fence 引用已失效（对应旧图像），全部重置为 VK_NULL_HANDLE
    m_imagesInFlight.assign(m_swapChainImages.size(), VK_NULL_HANDLE);
}

