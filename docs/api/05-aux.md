# 05 辅助模块（场景 / 性能 / 输入 / 资源 / 预制件）

**头文件**：[ecs/scene.h](../../src/ecs/scene.h) · [ecs/profiler.h](../../src/ecs/profiler.h) · [ecs/input_state.h](../../src/ecs/input_state.h) · [ecs/asset_manager.h](../../src/ecs/asset_manager.h) · [ecs/prefab.h](../../src/ecs/prefab.h)

---

## 5.1 Scene / SceneManager —— 多场景管理

把「一组实体 + 装配逻辑 + 生命周期」封装为 Scene 对象。**引擎不内置任何演示场景**，场景由用户在 `VulkanApp::registerScenes()` 中自定义注册。

### Scene 抽象基类

```cpp
class Scene {
public:
    virtual const char* name() const = 0;   // switchTo 查找用
    virtual void onEnter() {}               // 进入：创建实体
    virtual void onExit() {}                // 退出：自定义清理
    virtual void onUpdate(float dt) {}      // 每帧场景级逻辑
    Coordinator& owner();
    template <typename T> T* ctxAs();       // 用户上下文（类型不匹配返回 nullptr）
    const std::any& ctx() const;
};
```

### SceneManager

```cpp
void add(std::unique_ptr<Scene> scene);
template <typename T, typename... Args> T* add(Args&&... args);
bool switchTo(Coordinator& coord, const std::string& name, const std::any& userCtx = {});
void update(float dt);
Scene* current();
bool has(const std::string& name) const;
size_t size() const;
std::vector<std::string> listNames() const;
```

### 切换流程

`onExit()` → `coord.clear()` → 新场景注入 owner+userCtx → `onEnter()`。
EventBus 订阅在场景切换间持久存在（Coordinator 不被 clear）。

---

## 5.2 Profiler —— 性能分析

### 公开接口

```cpp
class Profiler {
    void frameBegin();  void frameEnd();
    void record(const char* name, double ms);   // ScopedTimer 调用
    struct SlotSnapshot { std::string name; double avgMs, maxMs, minMs, lastMs; uint64_t calls; };
    std::vector<SlotSnapshot> snapshot() const;  // 按 avgMs 降序
    double getFrameMs() const;  double getAvgFrameMs() const;  uint64_t getSnapFrameCount() const;
    void setWindowSeconds(double s);  void clear();
};
```

### ScopedTimer + PROFILE_SCOPE

```cpp
class ScopedTimer { ScopedTimer(Profiler*, const char* name); ~ScopedTimer(); };
#define PROFILE_SCOPE(profiler, name) ::ecs::ScopedTimer PROFILER_CONCAT(scopedTimer_, __LINE__)(profiler, name)
```

系统 `onUpdate` 首行写 `PROFILE_SCOPE(m_profiler, name());` 自动计时。DebugSystem 每 0.5s 上屏（最多 8 行系统耗时 + 帧平均）。

---

## 5.3 InputState / InputMapper

### InputState —— 原始键鼠状态

```cpp
struct InputState {
    std::array<bool, 400> keys{};
    double mouseX, mouseY, mouseDeltaX, mouseDeltaY;
    bool mouseLeft, mouseRight, mouseMiddle;
    int windowWidth, windowHeight;
    bool key(int k) const;      // 越界返回 false
    void clearDeltas();
};
```

### InputMapper —— 语义动作映射 + 边沿检测

```cpp
void bind(const std::string& action, int key);
void unbind(const std::string& action);
void rebind(const std::string& action, int newKey);
bool action(const std::string& name) const;         // 当前激活
bool justPressed(const std::string& name) const;    // 本帧刚按下
bool justReleased(const std::string& name) const;   // 本帧刚释放
void endFrame();                                    // 帧末推进边沿状态
void setInputState(const InputState* s);
std::vector<std::string> listActions() const;
const std::vector<int>* keysFor(const std::string& action) const;
```

### 内置默认动作（VulkanApp::registerInputActions）

| 动作 | 键 |
|------|-----|
| forward/backward/left/right | W/S/A/D + 方向键 |
| up/down | Q/E |
| jump/crouch/sprint | Space/Ctrl/Shift |
| rot_cw/rot_ccw | R/F |
| quit | ESC |

---

## 5.4 AssetManager —— 资源工厂

```cpp
void init(VkDevice, VkPhysicalDevice, VkCommandPool, VkQueue,
          VkDescriptorSetLayout meshSetLayout, VkDescriptorPool meshPool);
void init(const RenderDevice& dev, VkDescriptorSetLayout meshSetLayout,
          VkDescriptorPool meshPool);   // ⭐ v1.0.1 便捷重载（一句柄集）

std::unique_ptr<Mesh3D> createCube(float size, float r, float g, float b) const;
std::unique_ptr<Mesh3D> createPolyhedron(Polyhedron::Type, float scale, ...) const;
std::unique_ptr<Mesh3D> createSphere(float scale, ...) const;       // = Icosahedron
std::unique_ptr<Mesh3D> createOctahedron(float scale, ...) const;
std::unique_ptr<Shape>  createCircle(float cx, float cy, float radius,
                                     int segments, float r, float g, float b) const;

using MeshFactory = std::function<std::unique_ptr<Mesh3D>()>;
void registerMeshTemplate(const std::string& name, MeshFactory factory);
std::unique_ptr<Mesh3D> instantiate(const std::string& name) const;  // 新实例，已 upload
bool hasTemplate(const std::string& name) const;
```

> Mesh3D 有可变状态不能共享，故 AssetManager 缓存「模板工厂」而非实例。

---

## 5.5 PrefabRegistry —— 预制件

```cpp
struct PrefabCtx { std::any userData; };
using PrefabFactory = std::function<void(Coordinator&, Entity, const PrefabCtx&)>;

void add(const std::string& name, PrefabFactory factory);
Entity instantiate(Coordinator& coord, const std::string& name, const PrefabCtx& ctx = {}) const;
Entity instantiateNamed(Coordinator& coord, const std::string& name, const std::string& tagName,
                        const PrefabCtx& ctx = {}) const;
bool has(const std::string& name) const;
size_t size() const;
```

### 内置预制件（VulkanApp::registerPrefabs）

| 名字 | 组件 |
|------|------|
| `particle` | Shape2DComponent + Movement + Lifetime |
| `sun_cube` | Mesh3DComponent（橙色立方体） |
| `planet` | Mesh3DComponent（彩色多面体） |

---

## 附录：调试技巧

1. **Vulkan Validation Layers 必开**：debug 构建 `createVulkanInstance()` 请求 `VK_LAYER_KHRONOS_validation`；红色 ERROR 零容忍修复。
2. **帧泄漏/双重 vkMapMemory**：在 `uploadToBuffer` 前后计数，每帧递增 → Shape 没开脏标志。
3. **UI 事件不触发**：检查 `contains()` 局部坐标转换（父 transform 未累加）。
4. **GLFW 宏污染**：`#include <windows.h>` 放最后；CMake 已加 `/D WIN32_LEAN_AND_MEAN /D NOGDI`。
