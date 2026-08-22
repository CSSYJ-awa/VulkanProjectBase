/**
 * texture.h —— 纹理贴图系统（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 从内存 RGBA 数据创建 GPU 纹理（VkImage + View + Sampler + 描述符集）
 *   - 从文件加载纹理：TGA（无依赖解码）/ BMP（24-bit 无压缩）
 *   - 程序化纹理：棋盘格 / 渐变 / 噪声（无需资源文件）
 *   - 通用描述符集布局（COMBINED_IMAGE_SAMPLER，binding 0），可直接绑定到
 *     Pipeline2D/3D/自定义管线
 *
 * 依赖：engine/vulkan_util.h（图像创建/布局转换工具）
 */
#pragma once

#include "render/render_device.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Texture —— GPU 纹理资源
// ============================================================================
class Texture
{
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // ─── 创建 ────────────────────────────────────────────────────────────
    // 从内存 RGBA 数据创建（w*h*4 字节）。data 为 nullptr 时仅分配显存（不填充）。
    static std::unique_ptr<Texture> create(const RenderDevice& dev,
                                           uint32_t w, uint32_t h,
                                           const void* rgba,
                                           VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

    // 从文件加载纹理（支持 TGA / BMP，无第三方依赖）
    static std::unique_ptr<Texture> loadFromFile(const RenderDevice& dev,
                                                 const std::string& path);

    // ─── 程序化纹理（无需资源文件） ──────────────────────────────────────
    static std::unique_ptr<Texture> createChecker(const RenderDevice& dev,
                                                  uint32_t w, uint32_t h,
                                                  uint32_t cells,
                                                  float c1r, float c1g, float c1b,
                                                  float c2r, float c2g, float c2b);
    static std::unique_ptr<Texture> createGradient(const RenderDevice& dev,
                                                   uint32_t w, uint32_t h,
                                                   bool vertical = true);
    static std::unique_ptr<Texture> createNoise(const RenderDevice& dev,
                                                uint32_t w, uint32_t h,
                                                uint32_t seed = 0x5EEDu);

    // ─── 布局转换 ────────────────────────────────────────────────────────
    // 由调用方在录制命令缓冲时调用（内部经 beginSingleTimeCommands 提交）
    void transitionTo(const RenderDevice& dev, VkImageLayout newLayout) const;

    // ─── 访问 ────────────────────────────────────────────────────────────
    VkImage          image()           const { return m_image; }
    VkImageView      view()            const { return m_view; }
    VkSampler        sampler()         const { return m_sampler; }
    VkDescriptorSet  descriptorSet()   const { return m_descSet; }
    VkDescriptorSetLayout descriptorSetLayout() const { return m_descSetLayout; }
    uint32_t         width()           const { return m_width; }
    uint32_t         height()          const { return m_height; }
    VkFormat         format()          const { return m_format; }
    bool             valid()           const { return m_image != VK_NULL_HANDLE; }

    // 兼容 ShaderModule 语义：描述符布局（供管线 layout 复用）
    static VkDescriptorSetLayout createDefaultLayout(VkDevice device);

private:
    bool createFromRgba(const RenderDevice& dev, uint32_t w, uint32_t h,
                        const void* rgba, VkFormat format);
    void destroy();

    VkDevice            m_device         = VK_NULL_HANDLE;
    VkImage             m_image          = VK_NULL_HANDLE;
    VkDeviceMemory      m_memory         = VK_NULL_HANDLE;
    VkImageView         m_view           = VK_NULL_HANDLE;
    VkSampler           m_sampler        = VK_NULL_HANDLE;
    VkDescriptorSet     m_descSet        = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool    m_ownPool        = VK_NULL_HANDLE; // 独立创建时持有
    uint32_t            m_width          = 0;
    uint32_t            m_height         = 0;
    VkFormat            m_format         = VK_FORMAT_R8G8B8A8_UNORM;
    // 注意：transitionTo() 为 const（对外只读语义），layout 缓存用 mutable 允许内部更新
    mutable VkImageLayout m_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};

// ============================================================================
// 图像文件解码（纯 CPU，无 Vulkan 依赖，供 Texture::loadFromFile 使用）
// ============================================================================
namespace image_util {

struct DecodedImage
{
    uint32_t           width  = 0;
    uint32_t           height = 0;
    std::vector<uint8_t> rgba;   // w*h*4，行序从上到下
    bool               ok() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// 解码 24/32-bit 无压缩 BMP；失败返回 ok()==false 的 DecodedImage
DecodedImage decodeBmp(const std::vector<uint8_t>& data);
// 解码 8/16/24/32-bit TGA（无 RLE）；失败返回 ok()==false 的 DecodedImage
DecodedImage decodeTga(const std::vector<uint8_t>& data);
// 依据文件扩展名路由到对应解码器（.bmp / .tga）
DecodedImage decodeFile(const std::string& path);

// 程序化像素生成（RGBA 输出，供 Texture::createChecker 等使用）
void fillChecker(std::vector<uint8_t>& out, uint32_t w, uint32_t h,
                 uint32_t cells,
                 float c1r, float c1g, float c1b,
                 float c2r, float c2g, float c2b);
void fillGradient(std::vector<uint8_t>& out, uint32_t w, uint32_t h, bool vertical);
void fillNoise(std::vector<uint8_t>& out, uint32_t w, uint32_t h, uint32_t seed);

} // namespace image_util
