# 06 渲染引擎模块（v1.0.1 新增）

> 本系列模块位于 `src/render/`，每个模块对应一种独立渲染能力（API 相互独立、可单独使用），
> 并已由 `VulkanApp::createRenderModules()` 装配为一条完整渲染链。
> 全部模块共享 `RenderDevice` 句柄集合。

## 渲染链架构

```
┌──────────────────────────── 每帧渲染调度 ────────────────────────────┐
│ ① ShadowMap 深度 pass   （方向光视角渲染 m_meshes → 深度贴图）        │
│ ② RenderTarget 场景 RT  （Skybox 背景 → Mesh3D → Instancing → 粒子）  │
│ ③ PostFx 后处理          （RT 颜色附件 → 特效 shader → 主 pass）      │
│ ④ 2D/UI 叠加             （主 pass 内，letterbox 视口保持一致）       │
└───────────────────────────────────────────────────────────────────────┘
```

- **离屏 RT 尺寸** = 渲染区域（stretch 全屏 / letterbox 居中区域），随窗口缩放自动 resize。
- **RT 颜色/深度格式** 与主 pass 一致（`VulkanContext::imageFormat()/depthFormat()`），保证既有 2D/3D 管线可直接复用。
- **失败回退**：任一模块创建失败 → `destroyRenderModules()` → 回退 v1.0.0 传统直绘路径（引擎仍可运行）。

---

## 6.1 RenderDevice —— 渲染模块共享句柄

**头文件**：[render/render_device.h](../../src/render/render_device.h)

```cpp
struct RenderDevice {
    VkDevice           device;
    VkPhysicalDevice   physicalDevice;
    VkQueue            queue;
    VkCommandPool      commandPool;
    uint32_t           queueFamily;         // 目前仅占位
    VkDescriptorPool   descriptorPool;      // 可空：各模块自建描述符池，互不干扰
};
```

- 独立为基础设施头文件，**所有**渲染模块（含原有 `Shape`/`Mesh3D` 便捷重载）共享。
- 由 `VulkanContext::renderDevice()` 一键组装（`VulkanApp` 内 `m_renderDev = m_ctx->renderDevice()`）。

---

## 6.2 Texture —— 纹理贴图系统

**头文件**：[render/texture.h](../../src/render/texture.h) · **实现**：[render/texture.cpp](../../src/render/texture.cpp)

### 创建

```cpp
// 从内存 RGBA 数据创建（w*h*4 字节）；data 为 nullptr 仅分配显存
static std::unique_ptr<Texture> create(const RenderDevice& dev,
                                       uint32_t w, uint32_t h,
                                       const void* rgba,
                                       VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);

// 从文件加载（TGA / BMP，纯 CPU 无第三方依赖）
static std::unique_ptr<Texture> loadFromFile(const RenderDevice& dev,
                                             const std::string& path);
```

### 程序化纹理（无需资源文件）

```cpp
static std::unique_ptr<Texture> createChecker(const RenderDevice& dev, uint32_t w, uint32_t h,
                                              uint32_t cells, float c1r,float c1g,float c1b,
                                                              float c2r,float c2g,float c2b);
static std::unique_ptr<Texture> createGradient(const RenderDevice& dev, uint32_t w, uint32_t h,
                                               bool vertical = true);
static std::unique_ptr<Texture> createNoise(const RenderDevice& dev, uint32_t w, uint32_t h,
                                            uint32_t seed = 0x5EEDu);
```

### 布局转换与访问

```cpp
void transitionTo(const RenderDevice& dev, VkImageLayout newLayout) const;

VkImage          image()  const;   VkImageView view() const;
VkSampler        sampler() const;  VkDescriptorSet descriptorSet() const;
VkDescriptorSetLayout descriptorSetLayout() const;
uint32_t width() const;  uint32_t height() const;  VkFormat format() const;
bool valid() const;

// 通用 COMBINED_IMAGE_SAMPLER 布局（binding 0），多处复用
static VkDescriptorSetLayout createDefaultLayout(VkDevice device);
```

### 图像解码工具（image_util 命名空间）

```cpp
struct DecodedImage { uint32_t width, height; std::vector<uint8_t> rgba; bool ok() const; };
DecodedImage decodeBmp(const std::vector<uint8_t>& data);   // 24/32-bit 无压缩
DecodedImage decodeTga(const std::vector<uint8_t>& data);   // 8/16/24/32-bit 无 RLE
DecodedImage decodeFile(const std::string& path);           // 按扩展名路由
void fillChecker(std::vector<uint8_t>&, uint32_t w, uint32_t h, uint32_t cells, ...);
void fillGradient(std::vector<uint8_t>&, uint32_t w, uint32_t h, bool vertical);
void fillNoise(std::vector<uint8_t>&, uint32_t w, uint32_t h, uint32_t seed);
```

---

## 6.3 RenderTarget —— 离屏渲染目标

**头文件**：[render/framebuffer.h](../../src/render/framebuffer.h) · **实现**：[render/framebuffer.cpp](../../src/render/framebuffer.cpp)

自建 renderPass（颜色附件可采样 + 可选深度附件）与 Framebuffer，颜色附件输出 `VkDescriptorSet` 供后处理/自定义特效采样。

```cpp
static std::unique_ptr<RenderTarget> create(const RenderDevice& dev,
                                            uint32_t w, uint32_t h,
                                            bool withDepth = true,
                                            VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                                            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

bool resize(const RenderDevice& dev, uint32_t w, uint32_t h);   // 分辨率变化时重建

void begin(VkCommandBuffer cmd,
           const VkClearColorValue& clearColor = {0,0,0,1}) const;  // beginRenderPass
void end(VkCommandBuffer cmd) const;                                 // endRenderPass

VkRenderPass  renderPass() const;   VkFramebuffer framebuffer() const;
VkExtent2D    extent() const;       uint32_t width() const;  uint32_t height() const;
VkFormat      colorFormat() const;

VkImage         colorImage()  const;        VkImageView colorView() const;
VkSampler       colorSampler() const;       VkDescriptorSet colorDescriptorSet() const;
VkImage         depthImage()  const;        VkImageView depthView() const;
```

> 深度格式可参数化（与主 renderPass 一致以便管线复用）；颜色附件 finalLayout = SHADER_READ_ONLY_OPTIMAL，可直接采样。

---

## 6.4 PostFx —— 后处理特效链

**头文件**：[render/postfx.h](../../src/render/postfx.h) · **实现**：[render/postfx.cpp](../../src/render/postfx.cpp)

全屏三角形后处理（无顶点缓冲），把 `RenderTarget` 颜色附件作为输入纹理绘制到「当前已 begin 的 render pass」。

### 特效枚举

```cpp
enum class PostFxEffect {
    kNone       = 0,   // 原样输出
    kGrayscale  = 1,   // 灰度
    kInvert     = 2,   // 反色
    kBlur       = 3,   // 高斯模糊（9-tap 近似）
    kEdgeDetect = 4,   // 边缘检测（Sobel 近似）
    kSharpen    = 5,   // 锐化
    kBloom      = 6,   // 近似泛光（阈值高亮 + 邻域采样 + 强度混合）
};
```

### 公开接口

```cpp
// targetRenderPass：特效输出目标（RenderTarget::renderPass() 或主 renderPass）
void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
            const std::string& shaderDir);

void setEffect(PostFxEffect e);       // 特效模式
void setIntensity(float v);           // 特效强度（默认 1.0）
void setToneMap(bool on);             // 色调映射开关（曝光）
void setExposure(float e);            // 曝光（默认 1.0）
void setBloomThreshold(float t);      // 泛光阈值（默认 0.8）
PostFxEffect effect() const;  float intensity() const;

// 把 src 颜色附件作为输入绘制到"当前已 begin 的 render pass"
void apply(const RenderTarget& src, VkCommandBuffer cmd) const;
```

> 多特效串联：把 apply 输出到 RenderTargetB，再把 B 作为下一次 apply 输入（乒乓链）。

---

## 6.5 Skybox —— 程序化天空 + 环境光

**头文件**：[render/skybox.h](../../src/render/skybox.h) · **实现**：[render/skybox.cpp](../../src/render/skybox.cpp)

程序化天空背景（全屏渐变 + 太阳光斑 + 地平线暖光，随相机 yaw/pitch 旋转），**无 CubeMap/外部资源依赖**。管线关闭深度测试，保证作为背景。

```cpp
// targetRenderPass：绘制目标（RT renderPass 或主 renderPass）
void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
            const std::string& shaderDir);

// 在目标 render pass 已 begin 后调用（建议在 3D 网格绘制之前）
void draw(VkCommandBuffer cmd, const glm::mat4& view, float aspect) const;

// ─── 外观参数 ────────────────────────────────────
void setTopColor(const glm::vec3& c);  void setBottomColor(const glm::vec3& c);
void setSunColor(const glm::vec3& c);  void setSunDirection(const glm::vec3& dir); // 世界空间
void setSunIntensity(float v);         void setHorizonSpread(float v);

// ─── 环境光（供场景光照） ────────────────────────
void setAmbient(const glm::vec3& color, float intensity);
const glm::vec3& ambientColor() const;  float ambientIntensity() const;
```

---

## 6.6 ShadowMap —— 方向光阴影贴图

**头文件**：[render/shadow.h](../../src/render/shadow.h) · **实现**：[render/shadow.cpp](../../src/render/shadow.cpp)

depth-only render pass 生成深度贴图 + 深度比较采样器（PCF 软阴影基础），供自定义 3D 管线采样。

```cpp
// size×size 深度图 + 深度管线（shadow.vert/frag）
void create(const RenderDevice& dev, const std::string& shaderDir, uint32_t size = 2048);

// ─── 生成阶段（每帧） ────────────────────────────
void begin(VkCommandBuffer cmd) const;         // 深度清除为 1.0 + 设置 viewport/scissor
void end(VkCommandBuffer cmd) const;
void setLightMatrix(VkCommandBuffer cmd, const glm::mat4& lightVP);  // 更新 UBO + 绑定深度管线
VkPipelineLayout depthLayout() const;          // 供 Mesh3D::drawDepth 使用

// ─── 主 3D pass 绑定 ────────────────────────────
void bindToScene(VkCommandBuffer cmd, VkPipelineLayout sceneLayout) const; // binding 0 = shadow sampler
VkDescriptorSet       descriptorSet()       const;
VkDescriptorSetLayout descriptorSetLayout() const;

const glm::mat4& lightViewProj() const;  uint32_t size() const;  VkImageView depthView() const;

// 方向光正交 view-proj（sceneCenter 为中心、radius 半包围盒半径）
static glm::mat4 computeLightVP(const glm::vec3& lightDir,
                                const glm::vec3& sceneCenter, float radius);
```

**接入主 3D 场景的约定**：
1. 每帧 `setLightMatrix(cmd, lightVP)` → `begin(cmd)`；
2. 遍历场景网格调用 `Mesh3D::drawDepth(cmd, depthLayout(), model)` → `end(cmd)`；
3. 自定义 3D 管线在 fragment shader 中声明 `layout(binding=0) uniform sampler2D uShadowMap;`，
   用 `ShadowMap::descriptorSet()` 作为绑定描述符，将片段坐标变换到光空间后比较深度（PCF）。

> 深度管线 cullMode=NONE + depthBias(1.25/1.75) 抑制自阴影；深度图 finalLayout=SHADER_READ_ONLY；
> 比较采样器 compareEnable=VK_TRUE + borderColor=WHITE。

---

## 6.7 Instancing —— 实例化渲染

**头文件**：[render/instancing.h](../../src/render/instancing.h) · **实现**：[render/instancing.cpp](../../src/render/instancing.cpp)

大量同模型高效渲染：每实例 mat4 变换 + 每实例颜色；实例缓冲 HOST_VISIBLE 持久映射，CPU 每帧可更新，容量不足自动扩容。

```cpp
// targetRenderPass：绘制目标（RT 或主 renderPass）
void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
            const std::string& shaderDir);

// 上传基准网格顶点（9 float/顶点：pos3+normal3+color3）；可选索引
void uploadMesh(const RenderDevice& dev,
                const std::vector<float>& vertices,
                const std::vector<uint32_t>& indices = {});

// 每帧更新实例数据（count 与容量不一致时自动扩容；
// 实例缓冲持久映射，每帧更新零 vkMapMemory/vkUnmapMemory 开销）
void setInstances(const RenderDevice& dev,
                  const glm::mat4* transforms, size_t count,
                  const glm::vec4* colors = nullptr);

void setLight(const glm::vec3& lightDir, const glm::vec3& lightColor, float ambient);

// 绑定管线 + 描述符 + 绘制 count 个实例（目标 render pass 已 begin）
void draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
          size_t instanceCount) const;

size_t capacity() const;
```

**顶点输入布局**：binding0 = 网格（stride 36，vertex）；binding1 = 实例 mat4（instanced）；binding2 = 实例 vec4 颜色（instanced）。光照为 N·L 漫反射 + 环境光（UBO 提供 view/proj/光照）。

---

## 6.8 ParticleSystem —— CPU 粒子系统

**头文件**：[render/particles.h](../../src/render/particles.h) · **实现**：[render/particles.cpp](../../src/render/particles.cpp)

CPU 模拟（位置/速度/重力/生命周期/颜色渐变），点精灵渲染（gl_PointSize 公告板 + 软圆 alpha），可选叠加粒子纹理；加法混合、深度测试开、深度写入关。

```cpp
struct Particle {
    glm::vec3 position; float size;
    glm::vec3 velocity; float life;
    glm::vec4 color;     glm::vec4 startColor; glm::vec4 endColor;
    float maxLife;       float gravity;        float _pad[2];
};

void create(const RenderDevice& dev, VkRenderPass targetRenderPass,
            const std::string& shaderDir);
void setCapacity(const RenderDevice& dev, size_t max);           // 预分配（超出自动扩缓冲）
void setTexture(const RenderDevice& dev, const Texture* tex);    // nullptr = 纯色软圆

// 生成粒子：在 origin 附近、初速 baseVel±spread 随机散射
// 除 origin 外均带默认值，适用于"爆炸/火花"类快捷生成；精细控制请显式传参。
void spawn(const RenderDevice& dev,
           const glm::vec3& origin,
           const glm::vec3& baseVel = glm::vec3(0.0f),
           float spread = 0.5f,
           size_t count = 1,
           float life = 1.5f,
           float size = 0.05f,
           const glm::vec4& startColor = glm::vec4(1.0f),
           const glm::vec4& endColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
           float gravity = 0.0f);

void update(float dt);                                           // 推进模拟
void draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj,
          float viewportHeight) const;                           // 目标 pass 已 begin

size_t alive() const;  size_t max() const;  void clear();
```

> gl_PointSize = size × sizeScale × clip.w（透视缩放），sizeScale = viewportHeight/2；
> 未设置纹理时回退 1×1 白色纹理。

---

## 6.9 obj_loader —— OBJ 模型加载

**头文件**：[render/mesh_loader.h](../../src/render/mesh_loader.h) · **实现**：[render/mesh_loader.cpp](../../src/render/mesh_loader.cpp)

纯 CPU 解析 Wavefront OBJ（v/vt/vn/f），支持三角形/四边形、负索引、缺失法线自动平面法线（面积加权）。输出结构化 Mesh 数据，可喂给 Instancing::uploadMesh 或自定义管线。

```cpp
namespace obj_loader {
struct Vertex { glm::vec3 position; glm::vec3 normal; glm::vec2 uv; };
struct Mesh {
    std::string           name;
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    bool hasNormals;   // false = 已自动平面法线
    bool hasUV;
    size_t vertexCount() const;  size_t indexCount() const;  bool valid() const;
};

bool load(const std::string& path, Mesh& out);          // 解析文件
bool loadFromText(const std::string& text, Mesh& out);  // 解析内存文本
}
```

> 顶点去重（位置/法线/UV 全等三角判断）、负索引 resolve、四边形 fan 三角化。uv 供贴图管线使用。

---

## 6.10 既有接口扩展（v1.0.1）

| 位置 | 新增接口 | 说明 |
|------|---------|------|
| `Mesh3D::drawDepth(cmd, depthLayout, model)` | const | 阴影深度 pass 绘制（push constant model 64 字节） |
| `VulkanContext::depthFormat()` | 访问器 | 主深度格式（RenderTarget 对齐用） |
| `VulkanContext::framebuffer(imageIndex)` | 访问器 | 交换链帧缓冲（渲染链重新 begin 主 pass 用） |

## 6.11 着色器清单

| 着色器 | 用途 | 关键点 |
|--------|------|--------|
| `postfx.vert/frag` | 后处理 | 全屏三角形 `gl_VertexIndex`；7 模式 + 色调映射 `1-exp(-c*exposure)` |
| `shadow.vert/frag` | 阴影深度 | push constant(model) + lightVP UBO；frag 空 |
| `skybox.vert/frag` | 天空盒 | 相机旋转逆恢复世界方向；垂直渐变 + 太阳光斑 pow 96 |
| `instanced.vert/frag` | 实例化 | 8 属性（mesh3 + inst mat4×4 + inst color4）；N·L 光照 |
| `particle.vert/frag` | 粒子 | gl_PointSize 透视；gl_PointCoord 软圆 + 可选纹理 |

---

# 07 渲染高级功能（v1.0.2）

## 7.1 DebugRenderer —— 调试可视化（`render/debug_render.h/.cpp`）

世界空间线段渲染（LINE_LIST，每顶点颜色），零依赖便捷图元，用于调试碰撞盒 / 触发器区域 / 路径 / 法线等。

```cpp
// 在目标 render pass 内：clear → 收集 → render（每帧）
m_debug->clear();
m_debug->box(center, halfExtents, color);        // 盒线框
m_debug->aabb(minP, maxP, color);                // AABB
m_debug->sphere(center, radius, color, seg);     // 球线框
m_debug->circle(center, normal, radius, color);  // 任意朝向圆环
m_debug->grid(size, step, color);                // 地面网格
m_debug->axes(origin, length);                   // 坐标轴（RGB）
m_debug->normal(origin, dir, length, color);     // 法线
m_debug->line(a, b, color);
m_debug->render(cmd, view, proj);                // 批量上传 + 绘制
size_t lineCount() const;
```

| 特性 | 说明 |
|------|------|
| 批量上传 | 顶点缓冲持久映射 + 自动扩容（grow），clear/render 每帧一次上传 |
| 深度 | depthTest=ON、depthWrite=OFF（线不被自身遮挡写入） |
| 接入 | VulkanApp 已装配：`debugRenderer()` 返回指针，`renderSceneToRT` 每帧自动渲染；shader 为 `debug.vert/frag` |
| samples | create 可选 `samples` 参数（须与目标 renderPass 采样级别一致） |

## 7.2 Mesh3D 材质 / 雾效 / 半球环境光（v1.0.2）

### 接口

```cpp
void setTexture(const Texture* tex);          // 绑定材质纹理（三平面映射，无需 UV；nullptr=顶点色）
void setTextureScale(float scale);            // 三平面采样缩放（>0 启用纹理；0=关闭）
void setFog(bool on, const glm::vec3& color, float start, float end);  // 距离雾
void setHemisphere(const glm::vec3& up, const glm::vec3& down);        // 半球环境光
const Texture* texture() const;               // 当前纹理（批处理排序用）
```

### 着色器增强（`mesh3d.vert/frag`）

| 特性 | 原理 |
|------|------|
| **三平面映射材质** | 无 UV：按法线主轴权重 `pow(vec3(4.0))` 平滑混合 X/Y/Z 三个投影平面采样，`texScale>0` 启用，`texMix` 控制混合强度 |
| **距离雾** | view 空间深度 `smoothstep(fogStart→fogEnd)` 线性过渡到雾色 |
| **半球环境光** | `N.y*0.5+0.5` 在 `ambientDown→ambientUp` 间插值（法线越朝上越亮） |

### UBO 扩展（`MeshUBO`，std140，共 288 字节）

```
model(64) | view(64) | proj(64)          ← 原有
lightDir(16) | ambient(4)+pad(12) | lightColor(16)
fogColor(16, w=on) | fogParams(start,end,texScale,texMix)
ambientUp(16) | ambientDown(16)
```

> 描述符集更新为 **2 个 binding**（binding0=UBO，binding1=材质纹理 COMBINED_IMAGE_SAMPLER）；无材质时自动使用内部白色纹理兜底。VulkanApp 的 3D 网格描述符池已扩充 `COMBINED_IMAGE_SAMPLER` 容量。

## 7.3 MSAA 多重采样（v1.0.2）

### 配置（config.json）

```jsonc
"msaa_samples": 4    // 1 / 2 / 4 / 8；默认 4
```

### 实现

| 位置 | 说明 |
|------|------|
| `RenderTarget::create(..., samples)` | 离屏 RT 颜色/深度附件按 samples 多重采样，renderPass 自动附带 **resolve 附件**，渲染结束自动 resolve 到可采样单样本图像（后处理输入不受影响） |
| `Pipeline2D/3D/Text::create(..., samples)` | 管线 `rasterizationSamples` 参数化（须与目标 renderPass 一致） |
| `Skybox / Instancing / ParticleSystem / DebugRenderer::create(..., samples)` | 渲染链内管线同步 MSAA |
| `VulkanApp::createRenderModules` | 按 config 计算采样级别，并 **clamp 到物理设备支持的最大颜色采样数**（不支持自动降档）；渲染链 3D 使用专用 `m_pipe3DRT`（绑定 RT renderPass + MSAA），传统直绘路径仍用 `m_pipe3D`（主 pass 单样本） |

> 设备限制：`limits.framebufferColorSampleCounts` 决定可用的采样级别；请求过高自动降级并告警。

## 7.4 管线 samples 参数（v1.0.2 接口扩展）

```cpp
Pipeline2D::create(device, renderPass, extent, shaderDir,
                   topology = TRIANGLE_LIST,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
Pipeline3D::create(device, renderPass, extent, shaderDir,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
PipelineText::create(device, renderPass, extent, shaderDir,
                     VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
```
