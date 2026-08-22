# 04 ECS 实体组件系统

**头文件**：[ecs/coordinator.h](../../src/ecs/coordinator.h) · [ecs/components.h](../../src/ecs/components.h) · [ecs/systems.h](../../src/ecs/systems.h) · [ecs/event_bus.h](../../src/ecs/event_bus.h)

---

## 4.1 Coordinator + EntityBuilder

封装 EnTT 的 `entt::registry` + SystemManager + EventBus。低频 API 通过 `registry()` 暴露。

### 实体管理

```cpp
Entity createEntity();
Entity createEntity(const std::string& name);        // 带 Tag
EntityBuilder entity(const std::string& name = "");  // 链式创建
void destroyEntity(Entity e);                        // 发布 EntityDestroyed 事件
bool valid(Entity e) const;
```

### 组件管理

```cpp
template <typename T, typename... Args> T& addComponent(Entity e, Args&&... args);
template <typename T, typename... Args> T& replaceComponent(Entity e, Args&&... args);
template <typename T> void removeComponent(Entity e);
template <typename T> T& getComponent(Entity e);
template <typename T> const T& getComponent(Entity e) const;
template <typename T> bool hasComponent(Entity e) const;
template <typename... T> bool hasAllComponents(Entity e) const;
template <typename... T> bool hasAnyComponent(Entity e) const;
template <typename... T> auto view();          // 多组件视图（系统遍历）
```

### 系统 / 事件 / 便利

```cpp
SystemManager& systems();
void updateSystems(float dt);
EventBus& events();
Entity findByName(const std::string& name) const;
size_t entityCount() const;
void clear();                                     // 场景切换
template <typename Fn> void eachEntity(Fn&& fn) const;
```

### EntityBuilder

`Coordinator::entity()` 返回，`.with<Component>(args...)` 累积，`.build()` 完成创建。

### 标准事件

`EntityDestroyed{ entity }` —— `destroyEntity()` 自动发布。

---

## 4.2 14 个内置组件（纯数据 POD）

| 组件 | 字段 | 关联系统 |
|------|------|---------|
| **Transform** | position / rotationEuler / scale + modelMatrix() | Movement / Hierarchy / RenderSystem3D |
| **Movement** | velocity / angularVelocity | MovementSystem |
| **Mesh3DComponent** | unique_ptr\<Mesh3D\> + dirty | RenderSystem3D |
| **Shape2DComponent** | unique_ptr\<Shape\> | RenderSystem2D |
| **TextComponent** | unique_ptr\<TextRenderer\> + dirty | RenderSystemText |
| **Tag** | name | findByName |
| **Lifetime** | remaining（<0 = 永久） | LifetimeSystem |
| **Camera** | position/yaw/pitch/fov/aspect + viewMatrix/projMatrix + flyEnabled | CameraSystem |
| **Parent** | parent + localPosition/Rotation/Scale | HierarchySystem |
| **Input** | moveSpeed / rotationSpeed / isPlayer | InputSystem |
| **Spawner** | kind(kParticle/kCube) + interval/timer/maxAlive/alive + spawnLifetime/Origin/Radius/baseColor | SpawnerSystem |
| **Light** | kind(Direction/Point/Spot) + direction/intensity/color/range/spotAngle + isPrimary | LightingSystem |
| **Collider** | halfExtents + isTrigger/isStatic/colliding | ColliderSystem |
| **Follow** | target + offset + lerp + lookAt | CameraSystem |

### 碰撞事件结构

```cpp
struct CollisionEvent      { Entity a, b; glm::vec3 normal; float penetration; };  // 持续（每帧）
struct CollisionEnterEvent { Entity a, b; glm::vec3 normal; float penetration; };  // 边沿一次
struct CollisionExitEvent  { Entity a, b; };                                       // 边沿一次
```

---

## 4.3 12 个内置系统

### 每帧执行顺序（= 注册顺序，可用优先级调整）

```
InputSystem → CameraSystem → MovementSystem → ColliderSystem
→ LightingSystem → HierarchySystem → LifetimeSystem
→ SpawnerSystem → DebugSystem
→ RenderSystem3D/2D/Text (onUpdate 空，由 drawEcs* 调用 render)
```

### 逻辑系统

| 系统 | 查询组件 | 功能 |
|------|---------|------|
| **MovementSystem** | Transform + Movement | `position += velocity*dt`；`rotationEuler += angular*dt` |
| **LifetimeSystem** | Lifetime | 倒计时；<=0 销毁实体 |
| **HierarchySystem** | Parent + Transform | 父子层级世界变换（迭代传播） |
| **InputSystem** | Input + Transform | 方向键平移、Q/E 升降、R/F 自转 |
| **SpawnerSystem** | Spawner | 定时生成粒子/立方体 + Movement + Lifetime，可播种随机 |
| **DebugSystem** | debug_text 实体 | 每 0.5s 刷新 FPS/帧数/实体数 + 系统耗时多行上屏 |
| **CameraSystem** | Camera (+ Follow) | 自由飞行（WASD+右键）/ Follow 跟随；`static const Camera* findPrimary(const Coordinator&)` |
| **LightingSystem** | Light | 收集主方向光：`const Light* primaryLight() const; size_t lightCount() const;` |
| **ColliderSystem** | Transform + Collider | AABB 碰撞 O(n²)，三态边沿事件 + 双向位置修正（按 isStatic 分配穿透量） |

### 渲染系统（onUpdate 空，由 VulkanApp::drawEcs* 调用）

| 系统 | 查询组件 | render 参数 |
|------|---------|------------|
| **RenderSystem3D** | Transform + Mesh3DComponent | cmd / layout / view / proj |
| **RenderSystem2D** | Shape2DComponent | cmd / Pipelines{4 pipeline+layout} / viewport / scissor |
| **RenderSystemText** | TextComponent | cmd / pipeline / layout / viewport / scissor |

### 系统调度扩展

```cpp
// System 基类
void setProfiler(Profiler* p);  bool enabled() const;
void setEnabled(bool on);       void setPriority(int p);   // 越小越先（稳定排序）

// SystemManager
template <typename T> bool isRegistered() const;
bool setEnabledByName(const char* name, bool on);
bool setPriorityByName(const char* name, int p);
void setProfiler(Profiler* p);
```

- 同一类型只允许注册一次，重复 `LOG_WARN` 返回 nullptr。
- 每帧 update：按优先级 `std::stable_sort` → 跳过 `!enabled()` 的系统。

---

## 4.4 EventBus

```cpp
template <typename E, typename Callback> HandlerId subscribe(Callback&& cb);
void unsubscribe(HandlerId id);
template <typename E> void publish(const E& event);   // 同步派发
template <typename E, typename... Args> void emit(Args&&... args);
void clear();  size_t subscriberCount() const;
```

内置事件：`CollisionEvent` / `CollisionEnterEvent` / `CollisionExitEvent` / `EntityDestroyed`。

> 设计权衡：`std::type_index` 作 key，`std::function` 包装，同步派发，非线程安全（ECS 单线程主循环）。

---

## 4.5 完整事件系统（v1.0.2，`engine/event_system.h`）

升级为引擎级事件系统，**支持事件跨 ECS / 定时器 / 缓动 / 场景逻辑共用**；`ecs::EventBus` 成为其类型别名（代码兼容）。

```cpp
namespace events {
    class EventBus;                          // 完整事件总线
    EventBus& system();                      // 全局单例（引擎级）
}
// ecs::EventBus == events::EventBus（类型别名）
```

### 新增能力（相对 v1.0.1 EventBus）

| 接口 | 说明 |
|------|------|
| `subscribe<E>(cb, priority=0)` | 订阅；`priority` 越小越先执行（**稳定排序**，同优先级保持注册顺序） |
| `subscribeOnce<E>(cb, priority=0)` | 一次性订阅：触发后自动退订 |
| `unsubscribe(id)` | 按句柄退订 |
| `publish<E>(event)` | 同步派发 |
| `enqueue<E>(event)` / `enqueue<E>(args...)` | 延迟派发：排队（闭包捕获事件），不立即执行 |
| `dispatch()` | 执行全部排队事件（**主循环帧末调用**，跨系统/跨帧排程） |
| `emit<E>(args...)` | 构造并同步发布 |
| `clear()` / `queued()` / `subscriberCount()` | 管理 |

```cpp
// 全局单例：窗口/场景/用户自定义事件
events::system().subscribe<MyEvent>([](const MyEvent& e){ ... }, 10);
events::system().enqueue<MyEvent>({ 42 });   // 排队，帧末统一派发
events::system().dispatch();                 // 主循环末尾
// ECS 总线：Coordinator 内部实例，行为一致
m_ecs->events().subscribe<ecs::CollisionEvent>(...);
```

> VulkanApp 主循环已接入：`timers::system().update(dt)` → `tween::system().update(dt)` → ... → 帧末 `m_ecs->events().dispatch()` + `events::system().dispatch()`。

---

## 4.6 逻辑系统（v1.0.2）

### TimeState —— 全局时间缩放 + 固定时间步（`ecs/time_state.h`）

| 字段/方法 | 说明 |
|-----------|------|
| `timeScale` / `setTimeScale(s)` | 全局时间倍率（0 冻结，>0 加速/减速）；`dt = delta × timeScale` |
| `unscaledDt` | 未缩放原始 dt（渲染/输入用真实时间） |
| `fixedDt`（默认 1/60s） | 固定步长 |
| `consumeFixedSteps()` | tick() 后调用，返回本帧应执行的固定步数（含防死亡螺旋上限） |
| `fixedStepIndex` / `nextFixedStep()` | 当前固定步序号，推进到下一步 |

固定步进用法（确定性物理/逻辑）：
```cpp
uint32_t n = m_time.consumeFixedSteps();
for (uint32_t i = 0; i < n; ++i) {
    advancePhysics(m_time.fixedDt);   // 固定步长推进
    m_time.nextFixedStep();
}
// 渲染仍用 m_time.dt 插值，帧率无关
```

### TimerSystem —— 定时器 / 延时任务（`engine/timers.h`，全局单例 `timers::system()`）

| 接口 | 说明 |
|------|------|
| `after(seconds, cb)` | 一次性延时回调（返回 TimerId） |
| `every(seconds, cb)` | 循环定时回调 |
| `everyFrame(cb)` | 每帧回调（回调参数为帧 dt） |
| `cancel(id)` / `clear()` / `count()` | 取消 / 清空 / 计数 |
| `update(dt)` | 主循环推进（受全局时间缩放影响） |

> 负 id 取消语义：`cancel` 幂等；回调在 update 中执行，回调内可安全新增/取消定时器。

### TweenSystem —— 缓动动画（`engine/tween.h`，全局单例 `tween::system()`）

**非侵入式**：直接操作调用方持有的变量指针，无需继承或注册。

| 接口 | 说明 |
|------|------|
| `to(&target, toValue, duration, ease)` | float 标量补间 |
| `toVec3(&target, toVec3, duration, ease)` | vec3 补间 |
| `toColor4(&target[0], r,g,b,a, duration, ease)` | 颜色补间 |
| `cancel(id)` / `clear()` / `count()` | 管理 |
| `update(dt)` | 主循环推进 |

`enum class Ease`：`Linear / InQuad / OutQuad / InOutQuad / InCubic / OutCubic / InOutCubic / OutBack / OutElastic`。

```cpp
tween::system().to(&angle, 3.14f, 2.0f, tween::Ease::InOutQuad);
tween::system().toVec3(&pos, {10, 0, 0}, 1.5f, tween::Ease::OutCubic);
tween::system().toColor4(&color[0], 1, 0, 0, 1, 0.8f, tween::Ease::Linear);
```

### TriggerZone —— 区域触发器（`ecs/components.h` + `TriggerSystem`）

| 组件 | 说明 |
|------|------|
| `TriggerZone` | `kind`（kAABB / kSphere）、`halfExtents` / `radius`、`active`、`inside`（当前区域实体集合） |

`TriggerSystem`（已注册进引擎）每帧检测：区域中心 = `Transform.position`，对区域内/外所有带 Transform 的实体做包含测试，**进出区域（边沿一次）**发布：

- `TriggerEnterEvent { zone, other }` —— 进入
- `TriggerExitEvent { zone, other }` —— 退出

```cpp
m_ecs->events().subscribe<ecs::TriggerEnterEvent>([](const auto& ev){
    // ev.zone / ev.other
});
```

---

## 4.7 ECS 优化（v1.0.2）

| 优化 | 位置 | 说明 |
|------|------|------|
| **组件变更事件** | `Coordinator::autoPublishComponentEvents<T>()` | 为组件类型 T 开启：挂载 → `ComponentAdded<T>{entity, component}`，移除 → `ComponentRemoved<T>{entity}`（基于 EnTT `on_construct/on_destroy` 信号），只对关心的类型开启，零开销 |
| **实体列表缓存** | `Coordinator::aliveEntities()` | 存活实体 O(1) 缓存（create/destroy 自动维护），供批处理/统计每帧全量遍历 |
| **渲染批处理** | `RenderSystem3D::render` | 收集网格后**按材质纹理分组排序**（同纹理连续绘制，减少描述符集切换）；`setLight` 内部对相同值有脏标志缓存 |
| **固定时间步** | `TimeState` | 见 4.6：确定性逻辑按固定步长推进，渲染按 dt 插值 |

```cpp
// 组件变更事件示例
m_ecs->autoPublishComponentEvents<ecs::Collider>();
m_ecs->events().subscribe<ecs::ComponentAdded<ecs::Collider>>(
    [](const auto& ev){ /* ev.entity / ev.component */ });
```

---

## 4.8 SceneFactory —— 场景实体工厂（v1.0.2，`ecs/scene_factory.h`）

一键创建"常用场景实体"并自动装配组件，参数高度自定义，返回实体句柄。

```cpp
ecs::SceneFactory scene(*m_ecs, m_assets.get());
auto cam = scene.createCamera("main_cam", {0,6,12}, {0,1,0});   // 相机（lookAt 自动换算 yaw/pitch）
auto sun = scene.createDirectionalLight("sun", {-0.5,-1,-0.3}, {1,1,0.9}, 1.2f);
auto box = scene.createCube("box", {0,1,0}, 1.0f, {0.9f,0.4f,0.2f});   // 3D 网格（需 AssetManager）
auto fire = scene.createParticleSpawner("fire", {0,0,0}, 0.1f, 200, {0.9f,0.6f,0.2f});
auto goal = scene.createTriggerSphere("goal", {0,0,0}, 2.0f);           // 球形触发器
auto body = scene.createCollider("body", {0,0,0}, {0.5f,0.5f,0.5f});    // AABB 碰撞盒
```

| 方法 | 创建实体 |
|------|---------|
| `createEmpty(name)` | 空实体（可选 Tag） |
| `createCamera(name, pos, lookAt, isPrimary, flyEnabled)` | 相机（自动换算 yaw/pitch） |
| `createFollowCamera(name, target, offset, lerp, lookAt)` | 跟随相机（Camera + Follow） |
| `createDirectionalLight(name, dir, color, intensity, isPrimary)` | 方向光 |
| `createPointLight(name, pos, color, range, intensity)` | 点光源 |
| `createCube / createSphere / createPolyhedron / createMeshFromTemplate` | 3D 网格（Transform + Mesh3DComponent，需 AssetManager） |
| `createCircle(name, cx, cy, radius, segments, color)` | 2D 圆（NDC，需 AssetManager） |
| `createText(name, renderer)` | 文字（renderer 须已上传） |
| `createParticleSpawner / createCubeSpawner` | 生成器 |
| `createTriggerZone(name, center, halfExtents)` / `createTriggerSphere` | AABB / 球形触发器 |
| `createCollider(name, pos, halfExtents, isStatic, isTrigger)` / `attachCollider(e, ...)` | 碰撞盒 |
