/**
 * texture.cpp —— 纹理贴图系统实现
 */
#include "render/texture.h"

#include "engine/vulkan_util.h"
#include "engine/logger.h"

#include <cstring>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <random>

using namespace vulkan_util;

// ============================================================================
// Texture 实现
// ============================================================================

Texture::~Texture() { destroy(); }

VkDescriptorSetLayout Texture::createDefaultLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount  = 1;
    ci.pBindings     = &binding;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return layout;
}

std::unique_ptr<Texture> Texture::create(const RenderDevice& dev,
                                         uint32_t w, uint32_t h,
                                         const void* rgba, VkFormat format)
{
    auto tex = std::make_unique<Texture>();
    if (!tex->createFromRgba(dev, w, h, rgba, format))
        return nullptr;
    return tex;
}

std::unique_ptr<Texture> Texture::loadFromFile(const RenderDevice& dev,
                                               const std::string& path)
{
    image_util::DecodedImage img = image_util::decodeFile(path);
    if (!img.ok())
    {
        LOG_WARN("Texture", "loadFromFile", "解码失败 path=%s", path.c_str());
        return nullptr;
    }
    return create(dev, img.width, img.height, img.rgba.data());
}

std::unique_ptr<Texture> Texture::createChecker(const RenderDevice& dev,
                                                uint32_t w, uint32_t h, uint32_t cells,
                                                float c1r, float c1g, float c1b,
                                                float c2r, float c2g, float c2b)
{
    std::vector<uint8_t> px;
    image_util::fillChecker(px, w, h, cells, c1r, c1g, c1b, c2r, c2g, c2b);
    return create(dev, w, h, px.data());
}

std::unique_ptr<Texture> Texture::createGradient(const RenderDevice& dev,
                                                 uint32_t w, uint32_t h, bool vertical)
{
    std::vector<uint8_t> px;
    image_util::fillGradient(px, w, h, vertical);
    return create(dev, w, h, px.data());
}

std::unique_ptr<Texture> Texture::createNoise(const RenderDevice& dev,
                                              uint32_t w, uint32_t h, uint32_t seed)
{
    std::vector<uint8_t> px;
    image_util::fillNoise(px, w, h, seed);
    return create(dev, w, h, px.data());
}

void Texture::transitionTo(const RenderDevice& dev, VkImageLayout newLayout) const
{
    if (m_image == VK_NULL_HANDLE || newLayout == m_layout) return;
    transitionImageLayout(dev.device, dev.commandPool, dev.queue, m_image,
                          m_format, m_layout, newLayout);
    m_layout = newLayout;
}

bool Texture::createFromRgba(const RenderDevice& dev, uint32_t w, uint32_t h,
                             const void* rgba, VkFormat format)
{
    if (dev.device == VK_NULL_HANDLE || w == 0 || h == 0)
        return false;
    m_device = dev.device;
    m_width  = w;
    m_height = h;
    m_format = format;

    // 1. 图像（DEVICE_LOCAL，可被采样 + 可作拷贝目标）
    createImage2D(dev.device, dev.physicalDevice, w, h, format,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_image, m_memory);
    if (m_image == VK_NULL_HANDLE) return false;

    // 2. 上传像素数据（staging → 图像）
    if (rgba)
    {
        VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer staging; VkDeviceMemory stagingMem;
        createBuffer(dev.device, dev.physicalDevice, size,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMem);
        void* dst = nullptr;
        if (vkMapMemory(dev.device, stagingMem, 0, size, 0, &dst) != VK_SUCCESS)
        {
            destroyBuffer(dev.device, staging, stagingMem);
            destroy();
            return false;
        }
        std::memcpy(dst, rgba, static_cast<size_t>(size));
        vkUnmapMemory(dev.device, stagingMem);

        // 图像先转 TRANSFER_DST_OPTIMAL → 拷贝像素 → 转 SHADER_READ_ONLY_OPTIMAL
        transitionImageLayout(dev.device, dev.commandPool, dev.queue, m_image, format,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(dev.device, dev.commandPool, dev.queue, staging, m_image, w, h);
        transitionImageLayout(dev.device, dev.commandPool, dev.queue, m_image, format,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        destroyBuffer(dev.device, staging, stagingMem);
    }
    else
    {
        // 仅分配显存：转成可采样布局
        transitionImageLayout(dev.device, dev.commandPool, dev.queue, m_image, format,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // 3. 图像视图
    m_view = createImageView2D(dev.device, m_image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (m_view == VK_NULL_HANDLE) { destroy(); return false; }

    // 4. 采样器
    VkSamplerCreateInfo sci{};
    sci.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter               = VK_FILTER_LINEAR;
    sci.minFilter               = VK_FILTER_LINEAR;
    sci.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod                  = 1.0f;
    if (vkCreateSampler(dev.device, &sci, nullptr, &m_sampler) != VK_SUCCESS)
    { destroy(); return false; }

    // 5. 描述符布局
    m_descSetLayout = createDefaultLayout(dev.device);
    if (m_descSetLayout == VK_NULL_HANDLE) { destroy(); return false; }

    // 6. 描述符集（优先复用 RenderDevice 的池；无则自建）
    VkDescriptorPool pool = dev.descriptorPool;
    bool ownPool = false;
    if (pool == VK_NULL_HANDLE)
    {
        VkDescriptorPoolSize psize{};
        psize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        psize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pci{};
        pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.maxSets       = 1;
        pci.poolSizeCount = 1;
        pci.pPoolSizes    = &psize;
        if (vkCreateDescriptorPool(dev.device, &pci, nullptr, &pool) != VK_SUCCESS)
        { destroy(); return false; }
        ownPool = true;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_descSetLayout;
    if (vkAllocateDescriptorSets(dev.device, &ai, &m_descSet) != VK_SUCCESS)
    {
        if (ownPool) vkDestroyDescriptorPool(dev.device, pool, nullptr);
        destroy();
        return false;
    }
    if (ownPool) m_ownPool = pool;

    // 7. 写入描述符
    VkDescriptorImageInfo dii{};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = m_view;
    dii.sampler     = m_sampler;
    VkWriteDescriptorSet wds{};
    wds.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds.dstSet          = m_descSet;
    wds.dstBinding      = 0;
    wds.descriptorCount = 1;
    wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo      = &dii;
    vkUpdateDescriptorSets(dev.device, 1, &wds, 0, nullptr);

    return true;
}

void Texture::destroy()
{
    if (m_device == VK_NULL_HANDLE) return;
    if (m_view)       vkDestroyImageView(m_device, m_view, nullptr);
    if (m_sampler)    vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_image)      vkDestroyImage(m_device, m_image, nullptr);
    if (m_memory)     vkFreeMemory(m_device, m_memory, nullptr);
    if (m_ownPool)    vkDestroyDescriptorPool(m_device, m_ownPool, nullptr);
    m_view = VK_NULL_HANDLE;
    m_sampler = VK_NULL_HANDLE;
    m_descSetLayout = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE; m_memory = VK_NULL_HANDLE; m_ownPool = VK_NULL_HANDLE;
    m_descSet = VK_NULL_HANDLE;
}

// ============================================================================
// 图像文件解码
// ============================================================================
namespace image_util {

DecodedImage decodeBmp(const std::vector<uint8_t>& d)
{
    DecodedImage out;
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return out;
    const uint32_t dataOffset = d[10] | (d[11] << 8) | (d[12] << 16) | (d[13] << 24);
    const int32_t  w = static_cast<int32_t>(d[18] | (d[19] << 8) | (d[20] << 16) | (d[21] << 24));
    const int32_t  h = static_cast<int32_t>(d[22] | (d[23] << 8) | (d[24] << 16) | (d[25] << 24));
    const uint16_t bpp = static_cast<uint16_t>(d[28] | (d[29] << 8));
    if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32)) return out;
    if (dataOffset + static_cast<uint32_t>(w) * std::abs(h) * 4 > d.size()) return out;

    const uint32_t uw = static_cast<uint32_t>(w);
    const uint32_t uh = static_cast<uint32_t>(std::abs(h));
    out.width = uw; out.height = uh;
    out.rgba.resize(uw * uh * 4, 255);

    const uint32_t bytesPerPixel = bpp / 8;
    const uint32_t rowSize = ((uw * bytesPerPixel + 3u) / 4u) * 4u;
    for (uint32_t y = 0; y < uh; ++y)
    {
        // BMP 底部优先存储；h>0 时第 0 行为底部
        uint32_t srcY = (h > 0) ? (uh - 1 - y) : y;
        const uint8_t* row = d.data() + dataOffset + srcY * rowSize;
        for (uint32_t x = 0; x < uw; ++x)
        {
            const uint8_t* p = row + x * bytesPerPixel;
            uint8_t* dst = &out.rgba[(y * uw + x) * 4];
            dst[0] = p[2]; dst[1] = p[1]; dst[2] = p[0];
            dst[3] = (bpp == 32) ? p[3] : 255;
        }
    }
    return out;
}

DecodedImage decodeTga(const std::vector<uint8_t>& d)
{
    DecodedImage out;
    if (d.size() < 18) return out;
    const uint8_t  type   = d[2];              // 2 = uncompressed true-color
    const uint16_t w      = static_cast<uint16_t>(d[12] | (d[13] << 8));
    const uint16_t h      = static_cast<uint16_t>(d[14] | (d[15] << 8));
    const uint8_t  bpp    = d[16];
    const uint8_t  desc   = d[17];             // bit5 = top-left origin
    if (type != 2 || w == 0 || h == 0) return out;
    if (bpp != 24 && bpp != 32) return out;

    const uint32_t uw = w, uh = h;
    out.width = uw; out.height = uh;
    out.rgba.resize(uw * uh * 4, 255);

    const uint32_t bytesPerPixel = bpp / 8;
    const uint8_t* src = d.data() + 18;
    const bool topDown = (desc & 0x20) != 0;
    for (uint32_t y = 0; y < uh; ++y)
    {
        uint32_t srcY = topDown ? y : (uh - 1 - y);
        const uint8_t* row = src + srcY * uw * bytesPerPixel;
        for (uint32_t x = 0; x < uw; ++x)
        {
            const uint8_t* p = row + x * bytesPerPixel;
            uint8_t* dst = &out.rgba[(y * uw + x) * 4];
            dst[0] = p[2]; dst[1] = p[1]; dst[2] = p[0];
            dst[3] = (bpp == 32) ? p[3] : 255;
        }
    }
    return out;
}

DecodedImage decodeFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) return {};

    std::string lower = path;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".bmp")
        return decodeBmp(data);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".tga")
        return decodeTga(data);
    return {};
}

void fillChecker(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t cells,
                 float c1r, float c1g, float c1b, float c2r, float c2g, float c2b)
{
    out.assign(w * h * 4, 255);
    auto clamp255 = [](float v) -> uint8_t {
        if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
        return static_cast<uint8_t>(v * 255.0f + 0.5f);
    };
    if (cells == 0) cells = 1;   // 除零防护
    const uint32_t cellW = (w + cells - 1) / cells;
    const uint32_t cellH = (h + cells - 1) / cells;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            bool even = ((x / cellW) + (y / cellH)) % 2 == 0;
            float r = even ? c1r : c2r, g = even ? c1g : c2g, b = even ? c1b : c2b;
            uint8_t* p = &out[(y * w + x) * 4];
            p[0] = clamp255(r); p[1] = clamp255(g); p[2] = clamp255(b); p[3] = 255;
        }
}

void fillGradient(std::vector<uint8_t>& out, uint32_t w, uint32_t h, bool vertical)
{
    out.assign(w * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            // h/w 为 1 时避免除零（此时 t 取 0）
            float t = vertical ? (h > 1 ? static_cast<float>(y) / (h - 1) : 0.0f)
                               : (w > 1 ? static_cast<float>(x) / (w - 1) : 0.0f);
            uint8_t* p = &out[(y * w + x) * 4];
            p[0] = static_cast<uint8_t>(t * 255.0f);
            p[1] = static_cast<uint8_t>(t * 255.0f);
            p[2] = static_cast<uint8_t>(255 - t * 255.0f);
            p[3] = 255;
        }
}

void fillNoise(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t seed)
{
    out.assign(w * h * 4, 255);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    for (uint32_t i = 0; i < w * h; ++i)
    {
        uint8_t v = static_cast<uint8_t>(dist(rng));
        uint8_t* p = &out[i * 4];
        p[0] = p[1] = p[2] = v; p[3] = 255;
    }
}

} // namespace image_util
