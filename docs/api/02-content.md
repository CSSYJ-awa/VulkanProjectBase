# 02 图形内容系统（2D / 3D / 文字 / 字体）

**头文件**：[shapes/shape.h](../../src/shapes/shape.h) · [geometry3d/mesh3d.h](../../src/geometry3d/mesh3d.h) · [text/text_renderer.h](../../src/text/text_renderer.h) · [text/font.h](../../src/text/font.h)

---

## 2.1 Shape —— 2D 图元体系

「CPU 顶点（m_vertices）+ GPU 缓冲」两级存储：数据变化置 `m_dirty` → `generateVertices()` 重建 → `upload()` 仅设备/大小变化时重建缓冲（避免 use-after-free）→ `drawVBOOnly()` 批量绘制。

### 基类接口

```cpp
// 生命周期：不可拷贝、可移动
Shape(const Shape&) = delete;
Shape& operator=(const Shape&) = delete;
Shape(Shape&&) noexcept;
virtual ~Shape();

void upload(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue);
void upload(const RenderDevice& dev);          // ⭐ v1.0.1 便捷重载（一句柄集）
void drawVBOOnly(VkCommandBuffer, VkPipelineLayout) const;  // 仅绑 VB + PushConst + Draw

void  setColor(float r, float g, float b, float a = 1.0f);
const float* color() const;                 // float[4]

bool isLineTopology() const;                // Line（LINE_LIST）
bool isLineStripTopology() const;           // Wave（LINE_STRIP）
bool isFanTopology() const;                 // Circle / Polygon（TRIANGLE_FAN）
```

**顶点布局**：每顶点 **5 float**（`x, y, r, g, b`）。NDC 坐标：`x∈[-1,1]` 从左到右，`y∈[-1,1]` 从上到下（Vulkan 原生，不额外翻转 Y）。

### protected 钩子

```cpp
virtual void generateVertices() = 0;   // 派生类填充 m_vertices
std::vector<float> m_vertices;
bool m_dirty = true;
```

### 派生类

| 图元 | 构造签名（要点） | 拓扑 |
|------|-----------------|------|
| `Line` | `(x1,y1,x2,y2, r,g,b)` | LINE_LIST |
| `Triangle` | `(x1,y1,x2,y2,x3,y3, r,g,b)` | TRIANGLE_LIST |
| `Rectangle` | `(cx,cy,w,h, r,g,b)` | TRIANGLE_LIST |
| `Square` | `(cx,cy,side, r,g,b)`（继承 Rectangle） | TRIANGLE_LIST |
| `Circle` | `(cx,cy,radius, segments=32, r,g,b)` | TRIANGLE_FAN ⚡ |
| `Wave` | `(x1,y1,x2,y2, amplitude,frequency, samples, r,g,b)` | LINE_STRIP |
| `Polygon` | `(points: vector<pair<float,float>>, r,g,b)`（凸多边形） | TRIANGLE_FAN ⚡ |

⚡ FAN 拓扑顶点数 `segments+2` vs LIST 的 `3*segments`，顶点传输量 ↓ ~67%。

### 新增自定义图元（3 步约定）

1. 继承 `Shape`；2. 实现 `generateVertices()`（5 float/顶点）；3. 构造函数置 `m_fanTopology`（或 line 类）标志。加入 `m_shapes` 或 `Shape2DComponent` 即可被自动分组绘制。

---

## 2.2 Mesh3D —— 3D 网格体系

「CPU 顶点 + GPU 顶点缓冲 + UBO(MVP+光照) + 描述符集」四件套。比 Shape 多模型矩阵 / UBO / 每实例描述符集。

### 顶点布局（9 float/顶点）

```
float x, y, z;      // 局部坐标
float nx, ny, nz;   // 顶点法线（缺失则无光照）
float r, g, b;      // 顶点色
```

### 基类接口

```cpp
// ─── 变换 / 颜色 ─────────────────────────────────
void             setModel(const glm::mat4& m);
const glm::mat4& model() const;
void             setColor(float r, float g, float b, float a = 1.0f);
const float*     color() const;

// ─── 光照 ─────────────────────────────────────────
void setLight(const glm::vec3& lightDir,   // 世界空间光传播方向
              const glm::vec3& lightColor, // 光颜色 × 强度
              float ambient);              // 环境光强度

// ─── 上传 ────────────────────────────────────────
void upload(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue,
            VkDescriptorSetLayout descLayout, VkDescriptorPool descPool);
// descLayout 必须 == Pipeline3D::descriptorSetLayout()；descPool 支持 UNIFORM_BUFFER。
void upload(const RenderDevice& dev,          // ⭐ v1.0.1 便捷重载（一句柄集）
            VkDescriptorSetLayout descLayout, VkDescriptorPool descPool);

// ─── 绘制 ────────────────────────────────────────
void drawVBOOnly(VkCommandBuffer, VkPipelineLayout,
                 const glm::mat4& view, const glm::mat4& proj) const;
// 内部检测 view/proj/光照变化自动刷新 UBO（脏标志）

// ⭐ v1.0.1：阴影深度 pass 绘制（ShadowMap::begin 后调用）
void drawDepth(VkCommandBuffer, VkPipelineLayout depthLayout,
               const glm::mat4& model) const;
// depthLayout = ShadowMap::depthLayout()；push constant 偏移 0、大小 64（model）。
```

### UBO 脏标志机制

- `setModel` / `setLight` 参数变化时自动置脏；
- `drawVBOOnly` 内部检测 view/proj 变化 → 置脏 → 持久映射直接 memcpy → 清脏；
- 静态场景 + 静态相机：每帧 0 次 UBO 拷贝、0 次 vkMapMemory。

### 派生类

| 网格 | 构造 | 说明 |
|------|------|------|
| `Cube` | `(size=1.0, r,g,b)` | 6 面 × 2 三角 = 36 顶点，各面不同色相 |
| `Polyhedron` | `(Type, scale, r,g,b)` | `Tetrahedron`（4 面）/ `Octahedron`（8 面）/ `Icosahedron`（UV 球近似，12 段×6 环） |

### 新增自定义网格约定

继承 `Mesh3D` 实现 `generateVertices()`：9 float/顶点、**必须含法线**（法线为 0 时 N·L 恒 0）。上传后加入 `m_meshes` 或 `Mesh3DComponent` 即可被绘制；加入 `m_meshes` 的网格自动参与阴影深度 pass。

---

## 2.3 文字渲染：TextRenderer

继承自 Shape，把每个字形"点亮像素"渲染为小矩形，复用 Pipeline2D。支持像素字（1-bit）与平滑字（8-bit alpha 灰度抗锯齿），支持 `\n` 多行。

### 构造（两种模式）

```cpp
// 模式 A：像素坐标（推荐）
TextRenderer(const std::string& text, float px, float py,
             uint32_t pixelSize, uint32_t w, uint32_t h,
             float r = 1, float g = 1, float b = 1);
// 模式 B：NDC 坐标
TextRenderer(const std::string& text, float xNdc, float yNdc,
             float ndcSize, float r, float g, float b);
```

### 运行时修改

```cpp
void setText(const std::string& text);   // 置 m_dirty，下次 upload() 重建
void setPixelPosition(float px, float py, uint32_t w, uint32_t h);
void setPixelSize(uint32_t pixelSize, uint32_t w);
void setFont(Font* font);                // 运行时切换字体（nullptr = 默认 PixelFont）
Font* font() const;
```

> 文字变化频繁（每帧 FPS）时建议用 PipelineText + 字形图集，或主机可见持久映射缓冲。

---

## 2.4 字体系统：Font / PixelFont / SmoothFont / FontRegistry

### Font 抽象基类

```cpp
virtual const char* name() const = 0;
virtual void getGlyphPixels(char c,
    const std::function<void(int x, int y, uint8_t alpha)>& onPixel) const = 0;
virtual int glyphWidth() const = 0;
virtual int glyphHeight() const = 0;
```

### PixelFont（内置像素字，单例）

```cpp
static PixelFont& instance();   // 96 个 ASCII（0x20~0x7F），5×7 像素位图
```

### SmoothFont（GDI TTF 光栅化）

```cpp
SmoothFont(const std::string& faceName, int pixelSize);
// GDI CreateFont + GetGlyphOutline 光栅化 128 个 ASCII；逐像素 8-bit alpha 抗锯齿。
// 需要 gdi32 链接（build.json extra_libraries 默认已含）。
```

### FontRegistry（字体注册表，单例）

```cpp
static FontRegistry& instance();
Font* defaultFont();
void  setDefaultFont(Font* f);
Font* getOrCreateSmoothFont(const std::string& faceName, int pixelSize); // 缓存 + PixelFont 回退
void  clear();
```
