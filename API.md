# VulkanProjectBase —— 接口类用法手册 (API.md)

> 本手册详细说明每个核心类的**设计意图**、**公开接口**、**使用示例**与**最佳实践**。  
> 所有代码示例均为可复用的开发模板，可直接复制到自己的工程中。

---

## 目录

| 章节 | 说明 |
|------|------|
| [1. 应用层：VulkanApp](#1-应用层vulkanapp) | 顶层应用，组合所有模块 |
| [2. 渲染核心：VulkanContext](#2-渲染核心vulkancontext) | Vulkan 设备/交换链/命令缓冲封装 |
| [3. 工具函数：vulkan_util](#3-工具函数vulkan_util) | 缓冲/图像/着色器通用工具 |
| [4. 图形管线：Pipeline2D / 3D / Text](#4-图形管线pipeline2d--3d--text) | 三种预构建管线 |
| [5. 2D 图元：Shape 继承体系](#5-2d-图元shape-继承体系) | Line/Triangle/Rectangle/Square/Circle/Wave/Polygon |
| [6. 3D 网格：Mesh3D 继承体系](#6-3d-网格mesh3d-继承体系) | Cube / Polyhedron（5 种多面体） |
| [7. 文字渲染：TextRenderer](#7-文字渲染textrenderer) | 位图文字，NDC / 像素坐标双模式 |
| [8. UI 系统：元素 / 事件 / 控件](#8-ui-系统元素--事件--控件) | UiElement 树 + I* 接口 Mix-in |
| [9. UI 管理与 JSON 加载：UiManager / UiLoader](#9-ui-管理与-json-加载uimanager--uiloader) | 输入分发 + JSON 构建 UI 树 |

---

## 1. 应用层：VulkanApp

**头文件**：[vulkan_app.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/vulkan_app.h) / **实现**：[vulkan_app.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/vulkan_app.cpp)

### 设计意图
把「配置 → 窗口 → Vulkan → 管线 → 场景 → UI → 主循环 → 回调」全部组装起来的顶层类。用户只需：
1. 从 `config.json` 读参数；
2. 在 `createDemoScene()`（或自定义函数）里往 `m_shapes/m_meshes/m_titleText` 添加对象；
3. 在 UI 加载后用 `findByName("xxx")` 绑定按钮回调。

### 生命周期顺序（构造函数内）
```
loadConfig() → createWindow() → createVulkanInstance()
→ VulkanContext()
→ createEngineResources()   // 创建 5 条 Pipeline2D/3D/Text
→ createDemoScene()         // ⚠️ 用户自定义场景内容
→ UiManager + loadFromFile  // 构建 UI 树
→ 为 UI 控件绑定事件回调
→ setupCallbacks()
```

### 公开接口（用户常用）

| 方法 | 说明 |
|------|------|
| `void run()` | 主循环（ESC 退出，内部 `glfwPollEvents` + `renderFrame`） |
| `const AppConfig& config() const` | 读取 `config.json` 中的窗口标题/尺寸/aspect_mode/ui_config |

### 可覆盖的 6 个「用户入口」（都在 `private:` 里，直接修改源码）
```cpp
void createEngineResources();   // 如果你自定义了新管线（如阴影、后处理），在这里创建
void createDemoScene();         // 往 m_shapes/m_meshes/m_titleText 添加内容
void draw2DShapes();            // 自定义 2D 分组绘制（默认已按拓扑分组）
void draw3DMeshes();            // 自定义 3D 相机/投影/循环逻辑
void drawText();                // 自定义文字渲染顺序
void drawUI(VkCommandBuffer);   // 自定义 UI 渲染顺序
```

### 使用示例 —— 在 `createDemoScene()` 中添加内容
见下方 **Shape** / **Mesh3D** 章节的完整「场景构建模式」模板。

### 使用示例 —— 绑定 UI 回调
```cpp
// VulkanApp 构造函数末尾（loadFromFile 之后）
if (auto* root = m_ui->root())
{
    // 1) 按钮点击
    if (auto* btn = dynamic_cast<UiButton*>(root->findByName("my_start_btn")))
    {
        btn->setClickHandler([this]() {
            std::cout << "开始！" << std::endl;
            // 调用成员函数、操作 m_shapes / m_meshes 等
        });
    }
    // 2) 文本框输入变化
    if (auto* tb = dynamic_cast<UiTextBox*>(root->findByName("input_name")))
    {
        tb->setInputHandler([this](const std::string& s) {
            std::cout << "用户输入更新：" << s << std::endl;
        });
    }
    // 3) 按钮悬停变色（UiButton 已内置，无需手动；如需自定义覆盖 setHoverHandler）
}
```

### 最佳实践
- **场景对象一律用 `std::unique_ptr<Shape/Mesh3D>`**，`std::move` 进 `m_shapes/m_meshes`，VulkanApp 析构会自动按「GPU idle → 清空 shape/mesh → 销毁 pool → 销毁 pipeline → 销毁 ctx」顺序释放，完美避免 `VK_ERROR_DEVICE_LOST`。
- 自定义新类型**不要**修改 `draw2DShapes()`，优先用 `isLineTopology()/isFanTopology()` 标志，引擎会自动分组。
- 不要在任何地方直接 `vkDestroyBuffer`，Shape/Mesh3D 移动语义 + 析构已处理。

---

## 2. 渲染核心：VulkanContext

**头文件**：[engine/vulkan_context.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/vulkan_context.h)  
**实现**：[engine/vulkan_context.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/vulkan_context.cpp)

### 设计意图
封装所有 Vulkan「硬基础设施」：Surface → 物理/逻辑设备 → 队列 → 交换链 → ImageView → RenderPass → Framebuffer → CommandPool/CommandBuffer → Semaphore/Fence。  
对外只暴露 4 个操作：`beginFrame / endFrame / setResized / 访问器`。

### 公开接口

| 方法 | 返回值 | 说明 |
|------|--------|------|
| `beginFrame()` | `uint32_t` (imageIndex) | ① acquire 交换链图像 → ② 等待上一帧 fence → ③ beginRenderPass。**返回 `~0u` 表示交换链需要重建（已内部处理），调用方直接跳过本帧**。 |
| `endFrame()` | `void` | ① endRenderPass → ② submit → ③ present。自动处理 suboptimal 交换链重建。 |
| `setResized()` | `void` | GLFW 帧缓冲尺寸回调中**必须调用**，标记 m_framebufferResized 为 true，下一次 beginFrame 重建交换链。 |
| `device() / physicalDevice() / graphicsQueue() / presentQueue()` | Vulkan handle | 创建缓冲、上传、管线构建都要用。 |
| `renderPass()` | `VkRenderPass` | 构建所有 VkPipeline 都需要同一个 renderPass。 |
| `commandBuffer()` | `VkCommandBuffer` | 当前帧正在录制的命令缓冲（draw 系列函数里反复使用）。 |
| `commandPool()` | `VkCommandPool` | 给 `vulkan_util::uploadToBuffer`、`beginSingleTimeCommands` 用。 |
| `extent()` / `imageFormat()` | `VkExtent2D/VkFormat` | 构建 pipeline / pipeline viewport 用。 |
| `currentFrame()` | `uint32_t` | 若你需要按飞行帧管理「每帧独立的动态缓冲」，用这个索引（范围 `[0, MAX_FRAMES_IN_FLIGHT)`）。 |

### 使用示例 —— 在 Vulkan 之外的模块里创建缓冲 + 上传
```cpp
// 典型场景：自己写的「粒子系统」要创建 GPU 缓冲
void ParticleSystem::upload(VulkanContext& ctx)
{
    VkDevice dev      = ctx.device();
    VkPhysicalDevice pd = ctx.physicalDevice();
    VkCommandPool pool = ctx.commandPool();
    VkQueue q         = ctx.graphicsQueue();

    VkDeviceSize size = particles.size() * sizeof(Particle);
    vulkan_util::createBuffer(dev, pd, size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_particleBuf, m_particleMem);
    vulkan_util::uploadToBuffer(dev, pd, pool, q,
        particles.data(), size, m_particleBuf);
}
```

### 使用示例 —— 自定义新渲染通道
```cpp
// 要在 beginFrame 之后、endFrame 之前录制命令：
//   beginFrame() 内部已经 vkCmdBeginRenderPass(...) 了
// 所以你的 draw 函数直接拿 commandBuffer() 继续录制即可
VkCommandBuffer cmd = m_ctx->commandBuffer();
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, myPipeline);
// ... Bind VB / IB / Set / PushConst / Draw ...
// renderFrame() 最后会 endFrame()，不用管
```

### 关键参数

| 宏/常量 | 值 | 含义 |
|---------|-----|------|
| `MAX_FRAMES_IN_FLIGHT` | `2` | 双缓冲飞行（GPU pipeline 最大化）。若要改成 3，同时修改 `createSyncObjects()` / `recreateSwapChain()` 的 vectors。 |

### 最佳实践
- **不要手动销毁 VulkanContext 的任何内部资源**（swapchain/framebuffer/sync 等）。它在构造时创建、析构时清理、`~VulkanApp()` 里先 `vkDeviceWaitIdle` 再 reset，顺序正确。
- 交换链重建失败时 VulkanContext 会抛 `std::runtime_error`，在真正产品中你可以 catch 后降级退出，但调试期建议直接崩（能更早暴露 bug）。
- 如果你改 `beginFrame()` 的返回值语义，记得同步改 `VulkanApp::renderFrame()` 里的 `if (imageIndex == ~0u) return;`。

---

## 3. 工具函数：vulkan_util

**头文件**：[engine/vulkan_util.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/vulkan_util.h)  
**实现**：[engine/vulkan_util.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/vulkan_util.cpp)

### 设计意图
无状态的 namespace 级纯函数，封装 Vulkan 最常见的「样板代码」。

### 完整函数列表

```cpp
// ─── 查询 ────────────────────────────────────────
uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter,
                        VkMemoryPropertyFlags props);
// typeFilter = VkMemoryRequirements::memoryTypeBits；
// props = HOST_VISIBLE|HOST_COHERENT（CPU 可见）或 DEVICE_LOCAL（GPU 本地）

// ─── 缓冲区 ──────────────────────────────────────
void createBuffer(VkDevice device, VkPhysicalDevice pd, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                  VkBuffer& buffer, VkDeviceMemory& memory);
// 同时创建 VkBuffer + VkDeviceMemory，并 vkBindBufferMemory。
// size = 0 时未定义；务必先判断。

void destroyBuffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory);
// 等价于 vkDestroyBuffer + vkFreeMemory。buffer/memory 为 VK_NULL_HANDLE 安全。

void uploadToBuffer(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue,
                    const void* data, VkDeviceSize size, VkBuffer dstBuffer);
// 🎯 一次上传到 GPU：内部创建 stagging 缓冲（HOST_VISIBLE）→ copy → 销毁。
// 适用于「顶点/索引/UBO 初始化」。高频动态数据（每帧）请直接使用 HOST_VISIBLE 缓冲。

// ─── 着色器 ──────────────────────────────────────
VkShaderModule createShaderModule(VkDevice, const std::string& spvPath);
// 读取 .spv 文件；失败抛 std::runtime_error

VkShaderModule createShaderModuleFromMemory(VkDevice, const std::vector<uint8_t>& code);
// 从内存字节码（如嵌入式资源）创建；推荐生产环境使用，避免运行时读磁盘

// ─── 图像 ────────────────────────────────────────
void createImage2D(VkDevice, VkPhysicalDevice, uint32_t w, uint32_t h,
                   VkFormat format, VkImageUsageFlags usage,
                   VkMemoryPropertyFlags props, VkImage&, VkDeviceMemory&);

VkImageView createImageView2D(VkDevice, VkImage, VkFormat, VkImageAspectFlags);
// aspect = VK_IMAGE_ASPECT_COLOR_BIT 或 VK_IMAGE_ASPECT_DEPTH_BIT

void transitionImageLayout(VkDevice, VkCommandPool, VkQueue, VkImage, VkFormat,
                           VkImageLayout oldLayout, VkImageLayout newLayout);
// 图像布局转换：UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL 等

void copyBufferToImage(VkDevice, VkCommandPool, VkQueue,
                       VkBuffer, VkImage, uint32_t w, uint32_t h);
// 把 stagging buffer 的内容整块拷贝到图像

// ─── 一次性命令缓冲 ──────────────────────────────
VkCommandBuffer beginSingleTimeCommands(VkDevice, VkCommandPool);
void endSingleTimeCommands(VkDevice, VkCommandPool, VkQueue, VkCommandBuffer);
// 🎯 用法见下：

// ─── Push Constants 类型安全辅助（模板）──────────
template <typename T>
void pushConstants(VkCommandBuffer cmd, VkPipelineLayout layout,
                   VkShaderStageFlags stage, const T& value);
```

### 使用示例 —— 一次性命令缓冲（上传/拷贝/屏障）
```cpp
// 任何「临时使用一次」的 Vulkan 命令都用这个模式：
VkCommandBuffer cmd = vulkan_util::beginSingleTimeCommands(dev, pool);

    // （在这里写任意 vkCmd* 调用，例如管线屏障、copy buffer/image）
    VkBufferCopy region = { 0, 0, size };
    vkCmdCopyBuffer(cmd, staging, dst, 1, &region);

vulkan_util::endSingleTimeCommands(dev, pool, q, cmd);
// endSingleTimeCommands 内部：End → Submit → QueueWaitIdle → Free
```

### 使用示例 —— Push Constants 类型安全包装
```cpp
// 推荐替代原始的 vkCmdPushConstants 调用：
struct MyPushConst { glm::vec4 tint; float intensity; };
MyPushConst pc = { glm::vec4(1,0,0,1), 0.7f };
vulkan_util::pushConstants(cmd, layout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pc);
// 避免 sizeof 填错、指针取址、stage 漏加、偏移 0 忘记 ……
```

### 最佳实践
- `HOST_VISIBLE | HOST_COHERENT` 缓冲直接 `vkMapMemory`，**不要**用 `uploadToBuffer`（多一步 stagging 开销）。UBO、每帧更新的动态顶点数据都属于这种场景。
- `DEVICE_LOCAL` 缓冲才用 `uploadToBuffer`，例如模型加载后的静态顶点、压缩纹理等。
- 图像上传的**标准五步曲**（项目中 `BitmapFont` 实际用的就是这个）：
  1. `createImage2D` (DEVICE_LOCAL)
  2. stagging buffer + `uploadToBuffer` (填充 stagging 像素数据)
  3. `transitionImageLayout(UNDEFINED → TRANSFER_DST)`
  4. `copyBufferToImage`
  5. `transitionImageLayout(TRANSFER_DST → SHADER_READ_ONLY)`

---

## 4. 图形管线：Pipeline2D / 3D / Text

**头文件**：[engine/pipelines.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/pipelines.h)  
**实现**：[engine/pipelines.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/engine/pipelines.cpp)

### 4.1 Pipeline2D —— 通用 2D 图元管线

**顶点输入**：`vec2 pos + vec3 color` → 每顶点 5 float（`Shape::m_vertices` 布局）。  
**Push Constant**：`vec4 color`（偏移 0，Fragment 阶段，用于颜色/透明度调制）。  
**着色器**：`basic.vert.spv + basic.frag.spv`。

#### 公开接口
```cpp
void create(VkDevice device, VkRenderPass renderPass,
            VkExtent2D extent, const std::string& shaderDir,
            VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
// topology 可选：
//   TRIANGLE_LIST  (默认) - 矩形/三角形/独立三角
//   TRIANGLE_FAN         - 圆/凸多边形（项目内已自动选择 via isFanTopology）
//   LINE_LIST            - 线段（Line 类）
//   LINE_STRIP           - 连续折线（Wave 类）

VkPipeline       pipeline()        const;
VkPipelineLayout pipelineLayout()  const;  // 用于 vkCmdPushConstants、bindDescriptorSets
```

#### 使用示例 —— 新增自定义拓扑管线
```cpp
// 在 VulkanApp::createEngineResources() 里创建：
m_pipeLineLoop = std::make_unique<Pipeline2D>();
m_pipeLineLoop->create(dev, m_ctx->renderPass(), extent, shaderDir,
                       VK_PRIMITIVE_TOPOLOGY_LINE_LOOP);

// 在 draw2DShapes() 的分组循环里新增一组对应 isXxxTopology()：
// 在 Shape::Shape() 派生类构造函数中：
//   m_lineLoopTopology = true;    // （新增 bool 标志，做法与 m_fanTopology 相同）
```

---

### 4.2 Pipeline3D —— 3D 网格管线（MVP UBO）

**顶点输入**：`vec3 pos + vec3 color` → 每顶点 6 float（`Mesh3D::m_vertices` 布局）。  
**Push Constant**：`vec4 color`（Fragment，偏移 0）。  
**UBO (Binding 0)**：`mat4 model / mat4 view / mat4 proj`（Vertex Stage，见下方 MeshUBO）。

#### 公开接口
```cpp
void create(VkDevice, VkRenderPass, VkExtent2D, const std::string& shaderDir);

VkPipeline            pipeline()            const;
VkPipelineLayout      pipelineLayout()      const;
VkDescriptorSetLayout descriptorSetLayout() const;  // 给 Mesh3D 分配 descriptor set 用
```

#### UBO 布局（与 shader 严格一致，std140）
```cpp
// src/geometry3d/mesh3d.h
struct MeshUBO {
    glm::mat4 model;  // offset  0, size 64
    glm::mat4 view;   // offset 64, size 64
    glm::mat4 proj;   // offset 128, size 64
};
// 注意：GLM 默认列主序，与 shader 中的 mat4 一致，无需 transpose。
```

#### 最佳实践
- **永远不要手动分配 descriptor set**：给 Mesh3D::upload() 传一个公共池（VulkanApp::m_meshDescPool）。避免每个 Mesh3D 各自创建 pool（`MAX_DESCRIPTOR_SETS` 太小时会很快耗尽）。
- 创建池时预留 `8~16` 个 sets（比你预估的多 2 倍），后续新增 3D 模型不会重新创建 pool 并让所有已分配 set 失效。

---

### 4.3 PipelineText —— 文字管线（位图字体纹理采样）

**顶点输入**：与 Pipeline2D 相同（TextRenderer 继承 Shape，所以顶点矩形用 basic 管线同布局）。  
**Push Constant**：`vec4 color`（Fragment）。  
**Descriptor Set**：Binding 0 = `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`（字形图集）。

#### 公开接口
```cpp
void create(VkDevice, VkRenderPass, VkExtent2D, const std::string& shaderDir);

VkPipeline            pipeline()            const;
VkPipelineLayout      pipelineLayout()      const;
VkDescriptorSetLayout descriptorSetLayout() const;
VkDescriptorPool      descriptorPool()      const;

// 🎯 给一张字形图集（或任何 2D 纹理）分配描述符，内部从 descriptorPool 拿
VkDescriptorSet allocateDescriptorSet();
```

---

## 5. 2D 图元：Shape 继承体系

**头文件**：[shapes/shape.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/shapes/shape.h)  
**实现**：[shapes/shape.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/shapes/shape.cpp)

### 5.1 Shape 基类（所有 2D 图元的父亲）

#### 设计理念
「**CPU 顶点数据（m_vertices） + GPU 缓冲（m_buffer/m_memory）**」两级存储：
- 数据变化 → `generateVertices()` 重建 CPU 顶点（脏标志 `m_dirty`）
- 上传 GPU → `upload()` 只在「设备变化或缓冲大小变化」时**重建 VkBuffer**，否则复用旧缓冲，直接 `uploadToBuffer` 覆盖数据（⚠️ 关键：避免 GPU use-after-free）。
- 绘制 → `draw()`（完整 bind pipeline + viewport + scissor）或 `drawVBOOnly()`（批量，假设外部已预设置）。

#### 公开接口（所有派生类共享）

```cpp
// ─── 生命周期 ────────────────────────────────────
// 不可拷贝，可移动（偷取 GPU 资源）
Shape(const Shape&)             = delete;
Shape& operator=(const Shape&)  = delete;
Shape(Shape&& o) noexcept;
Shape& operator=(Shape&& o) noexcept;
virtual ~Shape();

// ─── 上传 ────────────────────────────────────────
void upload(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue);
// 调用时机：createDemoScene() 时调一次；如果属性改了再调一次。

// ─── 绘制（两种模式） ─────────────────────────────
// 模式 A：独立绘制（默认）— pipeline/viewport/scissor 完整设置
void draw(VkCommandBuffer, VkPipeline, VkPipelineLayout,
          const VkViewport&, const VkRect2D&) const;
// 模式 B：批量绘制（性能优化）— 只绑 VB + PushConst + Draw；
//          调用方须保证 pipeline/viewport/scissor 已预先设置
void drawVBOOnly(VkCommandBuffer, VkPipelineLayout) const;

// ─── 属性 ────────────────────────────────────────
void  setColor(float r, float g, float b, float a = 1.0f);
const float* color() const;   // 返回 float[4]

// ─── 拓扑查询（draw2DShapes 用） ─────────────────
bool isLineTopology()      const;   // Line
bool isLineStripTopology() const;   // Wave
bool isFanTopology()       const;   // Circle / Polygon（三角形扇）
```

#### 派生类：顶点布局
所有 `m_vertices` 使用相同的 **5-float 交织**：
```
m_vertices.push_back(x); m_vertices.push_back(y);
m_vertices.push_back(r); m_vertices.push_back(g); m_vertices.push_back(b);
// 每顶点大小 = 20 bytes。
```
NDC 坐标系：`x ∈ [-1, 1] 从左到右`，`y ∈ [-1, 1] 从上到下`（Vulkan 原生方向，**不额外翻转 Y**）。

#### protected 钩子
```cpp
virtual void generateVertices() = 0;  // 派生类必须实现：往 m_vertices 填顶点
std::vector<float> m_vertices;        // 直接访问
bool m_dirty = true;                  // 派生类属性修改后置 true，下次 upload() 会自动重建
```

---

### 5.2 Line —— 独立线段

```cpp
// 构造：端点 (x1,y1) → (x2,y2)，线颜色 rgb
Line::Line(float x1, float y1, float x2, float y2,
           float r, float g, float b);
// 拓扑：LINE_LIST  (2 顶点)
```

---

### 5.3 Triangle —— 填充三角形

```cpp
Triangle::Triangle(float x1, float y1, float x2, float y2,
                   float x3, float y3,
                   float r, float g, float b);
// 拓扑：TRIANGLE_LIST (3 顶点)
```

---

### 5.4 Rectangle / Square —— 轴对齐矩形

```cpp
Rectangle::Rectangle(float cx, float cy, float w, float h,
                     float r, float g, float b);
// cx/cy = 矩形中心 NDC，w/h = 全宽/全高

Square::Square(float cx, float cy, float side,
               float r, float g, float b);
// 继承自 Rectangle，side = 边长
// 拓扑：TRIANGLE_LIST  (2 三角形，6 顶点)
```

**修改后属性重建**：Rectangle 类派生后建议用「脏标志」模式：
```cpp
void setSize(float w, float h) { m_w = w; m_h = h; m_dirty = true; }
// 下次 upload() 会自动 generateVertices() + uploadToBuffer 覆盖
```

---

### 5.5 Circle —— 圆（⚡ 三角形扇优化）

```cpp
Circle::Circle(float cx, float cy, float radius,
               uint32_t segments = 32,
               float r, float g, float b);
// 拓扑：TRIANGLE_FAN
// 顶点数：1 + segments + 1 = segments + 2（中心 + 圆周 + 闭合）
// segments 建议范围 [3..64]，32 肉眼基本无锯齿。
```

⚡ 性能：相比原 `TRIANGLE_LIST` 的 `3*segments` 顶点，`FAN` 只需 `segments+2`，**顶点传输量 ↓ ~67%**。

---

### 5.6 Wave —— 正弦波形（折线）

```cpp
Wave::Wave(float x1, float y1, float x2, float y2,
           float amplitude, float frequency,
           uint32_t samples,
           float r, float g, float b);
// x1,y1-x2,y2 = 基线起点/终点（NDC）
// amplitude = 振幅（NDC 单位）
// frequency = 周期数（基线全长上有多少完整正弦波）
// samples   = 采样点数（LINE_STRIP 顶点数 = samples + 1）
//
// 示例：Wave( -0.95, -0.85, -0.55, -0.85, 0.05, 8.0, 64, 1, 0.6, 0.2 )
//       = 在底部偏左画一条振幅小、周期 8 的橙色波形。
// 拓扑：LINE_STRIP
```

---

### 5.7 Polygon —— 凸多边形（⚡ FAN 优化）

```cpp
Polygon::Polygon(const std::vector<std::pair<float, float>>& points,
                 float r, float g, float b);
// points.size() ≥ 3。要求凸多边形（三角化方式：points[0] 为扇心）。
// 顶点顺序：points[0..n-1] + points[1]（闭合扇）
// 拓扑：TRIANGLE_FAN
```

---

### 5.8 新增自定义图元 —— 完整模板

```cpp
#pragma once
#include "shape.h"

// 示例：圆角矩形（图元示例）
class RoundedRect : public Shape {
public:
    RoundedRect(float cx, float cy, float w, float h, float cornerR, uint32_t cornerSeg,
                float r, float g, float b)
        : m_cx(cx), m_cy(cy), m_w(w), m_h(h),
          m_cornerR(cornerR), m_cornerSeg(cornerSeg),
          m_r(r), m_g(g), m_b(b)
    {
        // 如果角段 + 矩形主体用 FAN 输出：
        //   m_fanTopology = true;
        // 如果是复杂组合，用 TRIANGLE_LIST（默认）：
    }

protected:
    void generateVertices() override {
        m_vertices.clear();
        // TODO: 你的顶点插入代码，每顶点 5 float (x,y,r,g,b)
    }

private:
    float m_cx, m_cy, m_w, m_h, m_cornerR;
    uint32_t m_cornerSeg;
    float m_r, m_g, m_b;
};
```

**注册到场景**（`createDemoScene()` 中）：
```cpp
auto rr = std::make_unique<RoundedRect>(0, 0, 0.6, 0.4, 0.05, 8, 0.5, 0.7, 1);
rr->upload(dev, pd, tmpPool, q);
m_shapes.push_back(std::move(rr));
```

---

## 6. 3D 网格：Mesh3D 继承体系

**头文件**：[geometry3d/mesh3d.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/geometry3d/mesh3d.h)  
**实现**：[geometry3d/mesh3d.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/geometry3d/mesh3d.cpp)

### 6.1 Mesh3D 基类

#### 设计理念
「**CPU 顶点 + GPU 顶点缓冲 + UBO(MVP) + 描述符集**」四件套。  
比 Shape 多三件：模型矩阵 `m_model`、UBO 缓冲、每实例 1 个描述符集。

#### 顶点布局（与 shader 的 `Mesh3DVertex` 严格一致）
```
m_vertices 每顶点 6 float：
  float x, y, z;   // 局部坐标 (pos)
  float r, g, b;   // 顶点色 (color)
```

#### 公开接口
```cpp
// ─── 属性（变换） ─────────────────────────────────
void               setModel(const glm::mat4& m);
const glm::mat4&   model() const;
void               setColor(float r, float g, float b, float a = 1.0f);
const float*       color() const;     // float[4]

// ─── 上传（比 Shape 多 2 个参数：descSetLayout + descPool） ─
void upload(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue,
            VkDescriptorSetLayout descSetLayout, VkDescriptorPool descPool);
// ⚠️ descSetLayout 必须 == m_pipe3D->descriptorSetLayout()
// ⚠️ descPool 必须支持 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER（见 VulkanApp::m_meshDescPool）

// ─── 绘制（两种模式） ─────────────────────────────
void draw(VkCommandBuffer, VkPipeline, VkPipelineLayout,
          const VkViewport&, const VkRect2D&,
          const glm::mat4& view, const glm::mat4& proj) const;

void drawVBOOnly(VkCommandBuffer, VkPipelineLayout,
                 const glm::mat4& view, const glm::mat4& proj) const;
// 批量绘制优化版（外部已设 pipeline/viewport/scissor）
```

#### ⚠️ UBO 脏标志机制（性能关键）
```cpp
void setModel(const glm::mat4& m) { m_model = m; m_uboDirty = true; }
```
- 仅当 `setModel()` 被调用后，`draw()` 内部才会 `vkMapMemory + memcpy + vkUnmapMemory`，然后**立即清 `m_uboDirty=false`**。
- 因此静态场景（模型不旋转/不移动）**每帧 0 次 CPU→GPU UBO 拷贝**。不要在每帧都调一次 `setModel(...)` 用同样的矩阵（浪费）。需要变换时**只在变化那帧调一次**即可。

---

### 6.2 Cube —— 立方体

```cpp
Cube::Cube(float size, float cx, float cy, float cz,
           float r, float g, float b);
// size = 边长（所有轴）
// cx,cy,cz = 中心局部坐标的偏移（等价于之后 setModel(translate) 初始位置）
// 顶点数：6 面 × 2 三角 × 3 顶点 = 36 顶点
```

---

### 6.3 Polyhedron —— 5 种正多面体

```cpp
enum class Type {
    Tetrahedron,   // 4 面体，4 顶点
    Cube6,         // 6 面体（等价 Cube，推荐直接用 Cube 类）
    Octahedron,    // 8 面体（正双四棱锥）
    Icosahedron,   // 20 面体（近似球，三角形面）
    Dodecahedron   // 12 面体（五边形面）
};

Polyhedron::Polyhedron(Type type, float size,
                       float r, float g, float b);
// size = 外接球半径
```

典型用途：`Icosahedron + size=0.35` 可作为低多边形球模型（细分球体可迭代对 Icosahedron 每条边中点投影到球面）。

---

### 6.4 新增自定义 3D 网格 —— 完整模板

```cpp
#pragma once
#include "mesh3d.h"
#include <glm/glm.hpp>
#include <cmath>

// 示例：参数化圆环（Torus）
class Torus : public Mesh3D {
public:
    Torus(float R_major,      // 主半径：管中心到原点
          float r_minor,      // 次半径：管子粗细
          uint32_t majorSeg,  // 主向分段（绕圆）
          uint32_t minorSeg,  // 次向分段（沿管周长）
          float r, float g, float b)
        : m_R(R_major), m_r(r_minor),
          m_major(majorSeg), m_minor(minorSeg),
          m_colR(r), m_colG(g), m_colB(b) {}

protected:
    void generateVertices() override {
        m_vertices.clear();
        const float TAU = 6.28318530718f;
        auto pushVtx = [this](const glm::vec3& p) {
            m_vertices.push_back(p.x); m_vertices.push_back(p.y); m_vertices.push_back(p.z);
            m_vertices.push_back(m_colR); m_vertices.push_back(m_colG); m_vertices.push_back(m_colB);
        };
        for (uint32_t i = 0; i < m_major; ++i) {
            for (uint32_t j = 0; j < m_minor; ++j) {
                // 环形 TRIANGLE_STRIP 展开的两个三角形
                float u0 = (i  ) * TAU / m_major;
                float u1 = (i+1) * TAU / m_major;
                float v0 = (j  ) * TAU / m_minor;
                float v1 = (j+1) * TAU / m_minor;
                auto P = [this](float u, float v) -> glm::vec3 {
                    float cu = std::cos(u), su = std::sin(u);
                    float cv = std::cos(v), sv = std::sin(v);
                    return { (m_R + m_r * cv) * cu, m_r * sv, (m_R + m_r * cv) * su };
                };
                // TRIANGLE_LIST：2 三角 per quad
                pushVtx(P(u0,v0)); pushVtx(P(u1,v0)); pushVtx(P(u0,v1));
                pushVtx(P(u1,v0)); pushVtx(P(u1,v1)); pushVtx(P(u0,v1));
            }
        }
    }

private:
    float    m_R, m_r;
    uint32_t m_major, m_minor;
    float    m_colR, m_colG, m_colB;
};
```

---

## 7. 文字渲染：TextRenderer

**头文件**：[text/text_renderer.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/text/text_renderer.h)  
**实现**：[text/text_renderer.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/text/text_renderer.cpp)

### 设计理念
继承自 Shape，复用 2D 顶点缓冲 + 上传 + 绘制逻辑。  
把字符串每个字符的 6×8 点阵中**每个点亮像素**变成一个 `1 char pixel × 1 char pixel` 的小矩形（颜色由 setColor 决定）。纯 Shape 实现，**不需要额外纹理**（位图字体数据见 `BitmapFont::bitmaps[128][8]`）。

### 两种构造模式

```cpp
// ─── 模式 A：像素坐标（✅ 推荐） ──────────────────
TextRenderer::TextRenderer(const std::string& text,
                           float px, float py,       // 像素坐标，左上 (0,0)=屏幕左上
                           uint32_t pixelSize,       // 每个字形像素的屏幕像素
                           uint32_t w, uint32_t h,   // 当前窗口宽高（做 pixel→NDC）
                           float r = 1, float g = 1, float b = 1);
// 例：24px 大的字放在屏幕 (24px, 24px)，窗口 1280×720：
//   TextRenderer("Hello", 24, 24, 24, 1280, 720, 1, 0.9, 0.2)

// ─── 模式 B：NDC 坐标 ─────────────────────────────
TextRenderer::TextRenderer(const std::string& text,
                           float xNdc, float yNdc,
                           float ndcSize,              // 每像素大小 NDC
                           float r, float g, float b);
```

### 运行时修改

```cpp
void setText(const std::string& text);
// 改文字后需要 upload() 吗？答：需要！setText 只改了 m_text 字符串，
// 内部自动置 m_dirty=true，下次 uploadShape() / s->upload() 会 generateVertices() 重建。

void setPixelPosition(float px, float py, uint32_t w, uint32_t h);
// 只移动位置（通常窗口缩放后用）。同样置 m_dirty=true。

void setPixelSize(uint32_t pixelSize, uint32_t w);
// 动态改变字号。w 用来换算 NDC。
```

### 最佳实践
- 文字变化**不频繁**（标题、FPS 显示）：setText → 调 `upload()` 一次。
- 文字变化**很频繁**（每帧改 FPS）：建议把 TextRenderer 改成「主机可见缓冲 + 持久映射」，或直接改用 PipelineText + 字形图集（性能更好）。当前 TextRenderer 是 `DEVICE_LOCAL` 缓冲，每帧 destroy/recreate/上传成本太高。

---

## 8. UI 系统：元素 / 事件 / 控件

### 8.1 UiElement —— 所有 UI 元素的基类

**头文件**：[ui/ui_element.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_element.h)

#### 树形结构
```
root (UiPanel, 可拖拽)
├── UiText (标签)
├── UiButton (按钮，点击回调)
├── UiTextBox (文本输入框，焦点/退格)
└── UiPanel (子面板，可拖拽)
    └── ... 嵌套任意层级
```

#### 公开接口（所有控件共享）

```cpp
// ─── 属性 ────────────────────────────────────────
void   setName(const std::string& s);
const  std::string& name() const;         // 通过名字查找（JSON 或代码创建时设置）

void   setPosition(float x, float y);     // 相对父元素的像素坐标
void   setSize(float w, float h);
float  x() const; float y() const; float width() const; float height() const;

void   setColor(float r, float g, float b, float a);  // 背景色
const  float* color() const;   // float[4]

void   setVisible(bool);
bool   visible() const;

// ─── 父子关系 ────────────────────────────────────
void addChild(std::unique_ptr<UiElement> child);
UiElement* parent() const;
const std::vector<std::unique_ptr<UiElement>>& children() const;
      std::vector<std::unique_ptr<UiElement>>& childrenMut();   // 可修改

// ─── 查找 ────────────────────────────────────────
UiElement* findByName(const std::string& name);   // 递归 DFS，找不到 → nullptr
// 注：名字重复返回第一个命中。

// ─── 命中测试 ────────────────────────────────────
bool contains(float x, float y) const;     // 像素坐标（相对根）

// ─── 脏标志渲染 ──────────────────────────────────
void markDirty();                           // 调用后，下次 draw() 会重建
bool dirty() const;                         // drawSelf 之前自动判断
```

#### 派生类 2 个 protected 钩子（必须实现）
```cpp
// 1) 绘制：调用 ctx 中合适的 pipeline 画自己
//    注意：viewport/scissor 已由 UiManager 预先设置为整个屏幕，
//    drawSelf 内部再按局部像素坐标 → NDC 画矩形/文字/边框
virtual void drawSelf(const UiRenderContext& ctx) = 0;

// 2) 事件（可选覆盖。不覆盖 = 返回 false，表示不消费事件，让父级/兄弟收）
virtual bool handleMouseEventSelf(const UiMouseEvent& e) { return false; }
virtual bool handleKeyEventSelf(const UiKeyEvent& e)     { return false; }
```

---

### 8.2 接口 Mix-in（多继承实现事件）

**头文件**：[ui/ui_event.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_event.h)

每种交互能力是**独立接口类**，控件按需多继承，避免「基类里塞 50 个虚函数」。

| 接口 | 虚函数 | 典型实现类 |
|------|--------|-----------|
| `IClickable` | `void onClick()` | UiButton |
| `IHoverable` | `void onHoverEnter() / onHoverLeave()` | UiButton（内置：悬停颜色变浅） |
| `IDraggable` | `void onDragStart(dx,dy) / onDragMove(dx,dy) / onDragEnd()` | UiPanel（内置：面板拖动） |
| `ITextInput` | `void onTextInput(const std::string& utf8_char)` | UiTextBox（内置：追加字符） |

每个接口还提供 `setXxxHandler(std::function<void(...)>)`，用于**外部绑定回调**（推荐，无需再派生）：
```cpp
// 示例：不派生新类，给一个 UiButton 绑定点击行为
if (auto* btn = dynamic_cast<UiButton*>(root->findByName("btn_ok")))
{
    btn->setClickHandler([]() {
        std::cout << "OK clicked!" << std::endl;
    });
}
```

---

### 8.3 4 个内置控件

#### A. UiText —— 纯文本标签
```cpp
// 最简洁的方式（JSON）：
//   { "type": "text", "name": "lbl_title", "x":10, "y":10, "width":300,
//     "text": "我的游戏", "color": [1,1,1, 0] }
//
// 代码方式：
auto lbl = std::make_unique<UiText>();
lbl->setName("lbl_scene");
lbl->setPosition(10, 50);
lbl->setSize(200, 24);
lbl->setText("当前关卡：1-1");
lbl->setColor(1, 1, 1, 0);   // a=0 表示透明背景（只画文字）
// 然后 panel->addChild(std::move(lbl)) 或 root 直接加
```

#### B. UiButton —— 点击 + 悬停变色
```cpp
// JSON：{ "type":"button","name":"btn_quit","x":10,"y":10,"w":100,"h":30,
//         "text":"QUIT","font_size":16,"color":[0.6,0.2,0.2,1.0] }
//
// 事件绑定（推荐）：
btn->setClickHandler([this]() { /* 退出逻辑 */ });
btn->setHoverHandler([](){ std::cout << "悬停" << std::endl; });
//
// 内置行为：
//   - 鼠标进入 → hover 颜色（颜色 * 1.2，clamp 到 [0,1]）
//   - 鼠标离开 → 恢复原色
//   - 左键按下松开（在 contains 内）→ 触发 onClick()
```

#### C. UiTextBox —— 文本输入（支持退格/焦点）
```cpp
// JSON：{ "type":"textbox","name":"tb_name","x":10,"y":90,"w":200,"h":30,
//         "text":"默认内容" }
//
// 输入变化绑定（推荐）：
tb->setInputHandler([this](const std::string& s) {
    std::cout << "用户输入：" << s << std::endl;
});
//
// 内置行为：
//   - 左键点击元素 → 获得全局焦点（同一时间最多 1 个焦点文本框）
//   - 点击空白/另一个文本框 → 失焦/切焦
//   - GLFW_KEY_BACKSPACE / DELETE → 删除最后 1 UTF-8 字符
//     （支持中文等多字节字符：自动回溯到起始字节）
//   - GLFW_KEY_ESC → 立即失焦
//   - onChar 回调的字符 → 追加到文本末尾
```

#### D. UiPanel —— 可拖拽容器（递归嵌套）
```cpp
// JSON：{ "type":"panel","name":"panel_tools","x":880,"y":40,
//         "width":380,"height":640, "draggable":true,
//         "color":[0.12,0.12,0.18,0.85], "children":[ ... ] }
//
// ⚠️ draggable=true 时，面板任意位置都可拖拽；
//    如果只想在"标题栏"拖动，需要派生自定义 UiPanel 并覆盖 IDraggable：
```

---

## 9. UI 管理与 JSON 加载：UiManager / UiLoader

### 9.1 UiManager —— 持有根 + 分发输入

**头文件**：[ui/ui_manager.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_manager.h)  
**实现**：[ui/ui_manager.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_manager.cpp)

#### 公开接口
```cpp
UiManager::UiManager();

// ─── UI 加载 ─────────────────────────────────────
// 方式 1：JSON 文件（推荐）
bool loadFromFile(const std::string& jsonPath);
// 方式 2：JSON 文本
bool loadFromText(const std::string& jsonContent);
// 方式 3：代码构建（直接 setRoot）
void setRoot(std::unique_ptr<UiElement> root);

// ─── 访问 ────────────────────────────────────────
UiElement* root();   // == nullptr 表示未加载
uint32_t   width() const;  // UI 坐标系宽度（默认 == 窗口宽度，像素）
uint32_t   height() const;

// ─── 渲染（VulkanApp::drawUI 内） ────────────────
void render(const UiRenderContext& ctx);
// ctx.pipelineFilled/pipelineLine/pipelineText 需提前准备好，
// UiText/UiButton/UiTextBox 内部会按需要使用。

// ─── 输入分发（VulkanApp 的 GLFW 回调调用这些） ─
void onMouseMove   (int x, int y);           // 像素坐标（GLFW 的屏幕像素）
void onMouseButton (int button, int action, int mods);  // button: 0=左,1=右,2=中
void onKey         (int key, int scancode, int action, int mods);
void onChar        (unsigned int codepoint); // onTextInput 的字符来源
```

#### ⚙️ UiRenderContext —— UI 绘制时需要的所有 Vulkan 句柄
```cpp
// ui/ui_element.h
struct UiRenderContext {
    VkCommandBuffer  cmd;
    VkPipeline       pipelineFilled;       // 填充矩形（TRIANGLE_LIST，背景/按钮/输入框）
    VkPipeline       pipelineLine;         // 边框（LINE_LIST，可选）
    VkPipeline       pipelineText;         // 文字
    VkPipelineLayout layoutFilled;
    VkPipelineLayout layoutLine;
    VkPipelineLayout layoutText;
    VkViewport       viewport;
    VkRect2D         scissor;
    uint32_t         screenW;
    uint32_t         screenH;
};
```
所有控件的 `drawSelf()` 都用这些 pipeline。若你要自绘圆/多边形，在这个 struct 加 `pipelineFilledFan` 并在 `VulkanApp::drawUI()` 传入。

---

### 9.2 UiLoader —— JSON → UI 元素树

**头文件**：[ui/ui_loader.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui_loader.h)  
**实现**：[ui/ui_loader.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui_loader.cpp)

#### 公开接口
```cpp
// 通常不需要直接用。UiManager::loadFromFile 内部会调用。
// 但如果你想在加载后做自定义调整，可以用：
static std::unique_ptr<UiElement> UiLoader::loadFromText(const std::string& json);
static std::unique_ptr<UiElement> UiLoader::loadFromFile(const std::string& path);
```

#### JSON 语法（完整字段参考）

```jsonc
{
  "ui": {
    // ─── 通用字段（所有元素都有）──────────────────
    "type": "panel | button | text | textbox",   // 必填
    "name": "唯一名称，用于 findByName",          // 建议必填
    "x": 10, "y": 10,                            // 相对父元素像素坐标
    "width": 300, "height": 40,
    "color": [r, g, b, a],                       // r,g,b,a ∈ [0,1]。a=0 透明背景

    // ─── panel 专属 ─────────────────────────────
    "draggable": true,                           // 默认 false
    "children": [ /* 递归同格式的子元素 */ ],

    // ─── text / button / textbox 专属 ────────────
    "text": "显示的文字",
    "font_size": 16,                             // 仅对 text/button 生效
    "placeholder": "未输入时显示的占位符"         // 仅对 textbox（可选扩展）
  }
}
```

#### 🔌 扩展：新增自定义控件到 UiLoader（3 步）

1. 派生一个 `UiMyWidget : public UiElement`（或多继承 IClickable 等）；
2. 在 `UiLoader::buildElement` 的 **if/else 链**中加一个分支：
   ```cpp
   if (type == "my_widget") {
       auto w = std::make_unique<UiMyWidget>();
       applyCommon(w.get(), node);
       // 读自定义字段：w->setFoo( node["foo"].asString() );
       return w;
   }
   ```
3. 编译。JSON 里 `"type": "my_widget"` 就会创建你的类。

---

## 附录 A：常见调用模式（Cheat Sheet）

### A.1 「Hello Triangle」最小场景
```cpp
void VulkanApp::createDemoScene() {
    VkDevice dev = m_ctx->device();
    VkPhysicalDevice pd = m_ctx->physicalDevice();
    VkCommandPool pool = m_ctx->commandPool();
    VkQueue q = m_ctx->graphicsQueue();

    auto tri = std::make_unique<Triangle>(0, 0.5, -0.5, -0.5, 0.5, -0.5,
                                          1.0f, 0.3f, 0.3f);
    tri->upload(dev, pd, pool, q);
    m_shapes.push_back(std::move(tri));
}
```

### A.2 「旋转立方体」动画
```cpp
// 声明成员：在 class VulkanApp 中加：
//   std::unique_ptr<Cube> m_rotCube;

// createDemoScene():
{
    // （先创建 m_meshDescPool，见 createDemoScene 现有代码）
    auto cube = std::make_unique<Cube>(1.0, 0,0,0, 0.5, 0.8, 0.4);
    cube->upload(dev, pd, tmpPool, q, m_pipe3D->descriptorSetLayout(), m_meshDescPool);
    m_rotCube = cube.get();    // ⚠️ 裸指针观察：cube 先 push 到 m_meshes
    m_meshes.push_back(std::move(cube));
}

// draw3DMeshes():
{
    static float t = 0;
    t += 0.01;    // 只在变化时 setModel → UBO dirty 置 true，更新一次
    m_rotCube->setModel( glm::rotate(glm::mat4(1), t, {0,1,0}) );
    // 然后按原流程循环 m_meshes，调 drawVBOOnly
    // 其他没 setModel 的网格？— 跳过 UBO 更新（性能）
}
```

### A.3 「自定义按钮」的 3 种绑定方式

```cpp
// ① 最简单：lambda 捕获外部变量（推荐）
btn->setClickHandler([this, name]() {
    std::cout << "玩家名字 = " << name << std::endl;
    // this → VulkanApp 成员函数可直接调用
});

// ② 成员函数指针：
btn->setClickHandler(std::bind(&VulkanApp::onStart, this));

// ③ 派生自定义按钮（如果行为很多，比如自定义绘制、自定义颜色）：
class StartButton : public UiButton {
protected:
    void onClick() override { /* 自定义 */ }
    void drawSelf(const UiRenderContext& ctx) override { /* 自定义绘制 */ }
};
```

### A.4 「窗口缩放后」重新上传所有 Shape / Mesh / Text
```cpp
// 如果你做了「letterbox 模式下 UI 坐标要重新映射」：
// 在 VulkanApp 的 onFramebufferResize（或 GLFW 回调）之后，
// 下一次 renderFrame 里 computeRenderArea() 自动算出新 viewport/scissor。
// 只有 Shape 的位置/尺寸**依赖窗口像素**时，才需要重新 upload：
//
// （TextRenderer 就是这种情况）
if (m_titleText) {
    m_titleText->setPixelPosition(24, 24, newWidth, newHeight);
    m_titleText->upload(dev, pd, pool, q);
}
```

---

## 附录 B：调试技巧

1. **Vulkan Validation Layers 必开**：CMake Configure 阶段如果 `NDEBUG` 没定义，`createVulkanInstance()` 会请求 `VK_LAYER_KHRONOS_validation`。控制台一旦出现红色 `ERROR`，**零容忍修复**（这些都是 GPU 崩溃的预告）。
2. **帧泄漏/双重 vkMapMemory**：在 `uploadToBuffer` 前后加 `std::cout` 计数，若每帧数量递增 → 说明某 Shape 没开脏标志。
3. **UI 事件不触发**：在 UiManager::onMouseButton 的 `findHit` 临时输出坐标和命中元素名，通常是 `contains()` 的局部坐标转换错误（父元素 transform 没累加）。
4. **GLFW 宏污染导致编译错误**（`Rectangle / Polygon` 不是类名）：CMake 已经加了 `/D WIN32_LEAN_AND_MEAN /D NOGDI`。如果你自己加了 `#include <windows.h>`，务必放在最后。

---

> **文档版本**：v1.0 (2026-08)  
> 维护：CSSYJ-awa · MIT License
