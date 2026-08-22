# 01 应用层与渲染核心

**头文件**：[vulkan_app.h](../../src/vulkan_app.h) · [engine/vulkan_context.h](../../src/engine/vulkan_context.h) · [engine/vulkan_util.h](../../src/engine/vulkan_util.h) · [engine/pipelines.h](../../src/engine/pipelines.h)

---

## 1.1 VulkanApp —— 顶层应用

组合全部引擎模块：配置 → 窗口 → Vulkan → 管线 → ECS → 场景 → UI → 主循环 → 回调。

### 生命周期顺序（构造函数内）

```
loadConfig() → createWindow() → createVulkanInstance()
→ VulkanContext()                    // 设备/交换链/renderPass(含深度)/帧缓冲
→ createEngineResources()            // Pipeline2D×4 / Pipeline3D / PipelineText + 描述符池
→ createRenderModules()              // ⭐ v1.0.1 渲染链模块（RT/后处理/天空/阴影/实例/粒子）
→ Coordinator() + registerEcsSystems()
→ bindEcsExternalContexts()
→ registerScenes() → createDemoScene()
→ UiManager → setupCallbacks()
```

### 公开接口

| 方法 | 说明 |
|------|------|
| `void run()` | 主循环（ESC 退出） |
| `const AppConfig& config() const` | 读取 `config.json`（window_title/width/height/aspect_mode） |
| `ecs::Coordinator* ecs() const` | ECS 协调器（创建实体/组件） |
| `VkInstance vulkanInstance() const` | 底层 Vulkan 实例 |
| `GLFWwindow* window() const` | 底层窗口句柄 |

### 可覆盖的「用户入口」（private，直接改源码）

| 方法 | 职责 |
|------|------|
| `registerEcsSystems()` | 注册 ECS 系统 |
| `bindEcsExternalContexts()` | 注入外部上下文（Input / Vulkan 句柄 / Profiler） |
| `registerScenes()` | 场景注册入口：`m_scenes->add<YourScene>()` + `switchTo(...)` |
| `createDemoScene()` | 轻量扩展点（直接 `m_ecs->entity(...)`） |
| `draw2DShapes()` / `draw3DMeshes()` / `drawText()` / `drawUI(cmd)` | 自定义绘制顺序 |

### ⭐ v1.0.1 渲染链（private，装配于 createRenderModules）

| 方法 | 职责 |
|------|------|
| `createRenderModules()` / `destroyRenderModules()` | 创建/释放渲染链模块（失败自动回退传统直绘路径） |
| `renderShadowPass(cmd)` | 阴影深度 pass：按主光方向 `computeLightVP` → 绘制全部 `m_meshes` |
| `renderSceneToRT(cmd, dt)` | 场景进离屏 RT：天空盒背景 → 3D 网格 → 实例化 → 粒子 |
| `beginMainPass(cmd, imageIndex)` | 渲染链路径下重新开始主 render pass |
| `currentCameraView()` | ECS 主相机或默认相机 view |

渲染链关键成员：`m_renderDev`（RenderDevice 共享句柄）· `m_sceneRT` · `m_postfx` ·
`m_skybox` · `m_shadow` · `m_instancing` · `m_particles` · `m_particleTex`。
`m_activeViewport/m_activeScissor` 表示当前绘制目标视口（RT 全尺寸或主 pass letterbox 区域），
所有 draw 函数统一使用，保证离屏/主 pass 两路切换正确。

---

## 1.2 VulkanContext —— 渲染后端

封装 Surface → 设备 → 队列 → 交换链 → RenderPass（含深度附件）→ Framebuffer → CommandPool → 同步对象。

### 公开接口

| 方法 | 说明 |
|------|------|
| `uint32_t beginFrame()` | 等待 fence → acquire 图像 → beginRenderPass。返回 `~0u` 表示交换链重建，跳过本帧 |
| `void endFrame()` | endRenderPass → submit → present（自动处理 suboptimal 重建） |
| `void setResized()` | GLFW 帧缓冲尺寸回调中必须调用 |
| `device() / physicalDevice() / graphicsQueue() / presentQueue()` | Vulkan 句柄 |
| `renderPass()` | 主 renderPass（含深度附件），构建管线用 |
| `commandBuffer()` / `commandPool()` | 当前帧命令缓冲 / 命令池 |
| `extent() / imageFormat()` | 交换链尺寸 / 颜色格式 |
| `depthFormat()` | ⭐ v1.0.1 新增：主深度格式（供 RenderTarget 对齐） |
| `renderDevice()` | ⭐ v1.0.1 新增：一键组装 `RenderDevice`（渲染模块共享句柄） |
| `framebuffer(uint32_t imageIndex)` | ⭐ v1.0.1 新增：交换链帧缓冲（渲染链重新 begin 主 pass 用） |
| `currentFrame()` | 每帧索引（范围 `[0, MAX_FRAMES_IN_FLIGHT)`） |

### 关键参数

| 常量 | 值 | 含义 |
|------|-----|------|
| `MAX_FRAMES_IN_FLIGHT` | `2` | 双缓冲飞行 |
| 深度格式 | `D32_SFLOAT → D24_UNORM_S8_UINT → D16_UNORM` | `findDepthFormat()` 按支持度降序 |

### 最佳实践

- 不手动销毁内部资源；析构顺序由 `~VulkanApp` 保证（`vkDeviceWaitIdle` 前置）。
- 交换链重建失败抛 `std::runtime_error`。
- `beginFrame` 返回 `~0u` 时直接 `return`（跳过本帧）。

---

## 1.3 vulkan_util —— 工具函数

无状态 namespace 纯函数：缓冲/图像/着色器创建上传 + **GPU 资源延迟销毁队列**。

### 查询

```cpp
uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter, VkMemoryPropertyFlags props);
```

### 缓冲区

```cpp
void createBuffer(VkDevice, VkPhysicalDevice, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                  VkBuffer&, VkDeviceMemory&);
void destroyBuffer(VkDevice, VkBuffer, VkDeviceMemory);   // 仅供引擎内部；业务请走延迟队列
void uploadToBuffer(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue,
                    const void* data, VkDeviceSize size, VkBuffer dst);
```

### 延迟销毁队列（GPU 垃圾回收）

```cpp
struct DeferredDestroy {
    VkDevice device; VkBuffer buffer; VkDeviceMemory memory;
    void* mapped;               // 非空则释放前 vkUnmapMemory
    VkDescriptorPool descPool;  // 非空则释放前回收 descSet
    VkDescriptorSet descSet;
};
void deferDestroyBuffer(VkDevice, VkBuffer, VkDeviceMemory, void* mapped = nullptr);
void deferDestroyDescriptorSet(VkDevice, VkDescriptorPool, VkDescriptorSet);
void flushDeferredDestroy(VkDevice);      // beginFrame 在 vkWaitForFences 后调用
void flushAllDeferredDestroy(VkDevice);   // 设备空闲后收尾
```

> **规则**：凡可能被上一帧 GPU 引用的缓冲/描述符集，释放必须走延迟队列，杜绝 `VK_ERROR_DEVICE_LOST`。

### 着色器 / 图像 / 命令

```cpp
VkShaderModule createShaderModule(VkDevice, const std::string& spvPath);
VkShaderModule createShaderModuleFromMemory(VkDevice, const std::vector<uint8_t>& code);
void createImage2D(VkDevice, VkPhysicalDevice, uint32_t w, uint32_t h,
                   VkFormat, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage&, VkDeviceMemory&);
VkImageView createImageView2D(VkDevice, VkImage, VkFormat, VkImageAspectFlags);
void transitionImageLayout(VkDevice, VkCommandPool, VkQueue, VkImage, VkFormat,
                           VkImageLayout old, VkImageLayout neu);
void copyBufferToImage(VkDevice, VkCommandPool, VkQueue, VkBuffer, VkImage, uint32_t w, uint32_t h);
VkCommandBuffer beginSingleTimeCommands(VkDevice, VkCommandPool);
void endSingleTimeCommands(VkDevice, VkCommandPool, VkQueue, VkCommandBuffer);
template <typename T> void pushConstants(VkCommandBuffer, VkPipelineLayout,
                                         VkShaderStageFlags, const T& value);
```

---

## 1.4 图形管线：Pipeline2D / 3D / Text

### Pipeline2D —— 通用 2D 图元管线

- 顶点输入：`vec2 pos + vec3 color`（5 float/顶点）；Push Constant：`vec4 color`（Fragment）；无描述符。
- NDC 坐标，Y 向下。

```cpp
void create(VkDevice, VkRenderPass, VkExtent2D, const std::string& shaderDir,
            VkPrimitiveTopology topology);   // TRIANGLE_LIST / TRIANGLE_FAN / LINE_LIST / LINE_STRIP
VkPipeline       pipeline()       const;
VkPipelineLayout pipelineLayout() const;
```

### Pipeline3D —— 3D 网格管线

- 深度测试 `COMPARE_OP_LESS`（深度附件见 §1.2）；顶点输入：`vec3 pos + vec3 normal + vec3 color`（9 float/顶点）。
- Push Constant：`vec4 color`（Fragment）；UBO (Binding 0)：model/view/proj + 光照。

```cpp
void create(VkDevice, VkRenderPass, VkExtent2D, const std::string& shaderDir);
VkPipeline            pipeline()            const;
VkPipelineLayout      pipelineLayout()      const;
VkDescriptorSetLayout descriptorSetLayout() const;   // 给 Mesh3D 分配描述符集
```

#### UBO 布局（与 mesh3d.vert 一致，std140）

```cpp
struct MeshUBO {
    glm::mat4 model;      //  0
    glm::mat4 view;       // 64
    glm::mat4 proj;       // 128
    glm::vec3 lightDir;   // 192  世界空间光传播方向
    float     ambient;    // 204  环境光强度
    glm::vec3 lightColor; // 208  光颜色 × 强度
    float     _pad;       // 220
};
```

### PipelineText —— 字形图集采样管线（预留）

- 顶点输入：`vec2 pos + vec2 tex`（4 float/顶点）；Binding 0 = COMBINED_IMAGE_SAMPLER。
- ⚠️ 与 TextRenderer 非同一体系：TextRenderer 用 Pipeline2D，PipelineText 供字形图集采样模式使用。

```cpp
void create(VkDevice, VkRenderPass, VkExtent2D, const std::string& shaderDir);
VkPipeline            pipeline()            const;
VkPipelineLayout      pipelineLayout()      const;
VkDescriptorSetLayout descriptorSetLayout() const;
VkDescriptorPool      descriptorPool()      const;
VkDescriptorSet       allocateDescriptorSet();
```
