# 更新日志（CHANGELOG）

本文件记录 VulkanProjectBase 的版本变更。格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 约定，版本遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

## [1.0.2] - 2026-08

### 渲染高级功能
- **DebugRenderer 调试可视化**（`render/debug_render.h/.cpp` + `debug.vert/frag`）：世界空间线段批量渲染（LINE_LIST、每顶点色、持久映射 + 自动扩容），便捷图元 `line/box/aabb/sphere/circle/grid/axes/normal`；depthTest=ON / depthWrite=OFF；`VulkanApp::debugRenderer()` 直接访问，`renderSceneToRT` 每帧自动渲染。
- **Mesh3D 材质贴图**（三平面映射 triplanar）：无需 UV，按法线主轴权重平滑采样 3 个投影面；`setTexture` / `setTextureScale`；描述符集升级为 2 binding（UBO + 材质纹理），无材质自动白色纹理兜底。
- **距离雾 + 半球环境光**：`setFog` / `setHemisphere`；MeshUBO 扩展至 288 字节（std140）；mesh3d.vert 输出 view 深度、mesh3d.frag 重写（半球光 + 三平面 + 距离雾）。
- **MSAA 多重采样**：`config.json` 新增 `msaa_samples`（1/2/4/8，默认 4）；`RenderTarget::create` 支持 samples（MSAA 附件 + 自动 resolve 到可采样图像）；`Pipeline2D/3D/Text` 与 Skybox/Instancing/ParticleSystem/DebugRenderer 管线 samples 参数化；渲染链 3D 专用 MSAA 管线（`m_pipe3DRT`），设备不支持自动降档。

### 逻辑高级功能（可复用 API）
- **完整事件系统**（`engine/event_system.h`）：订阅优先级（稳定排序）、`subscribeOnce` 一次性监听、`enqueue/dispatch` 延迟派发队列、全局单例 `events::system()`；`ecs::EventBus` 成为类型别名（代码兼容）。
- **Timer 定时器**（`engine/timers.h`）：`after` 延时 / `every` 循环 / `everyFrame` 每帧回调，负值取消语义，全局单例。
- **Tween 缓动动画**（`engine/tween.h`）：8 种缓动曲线，标量 / vec3 / 颜色补间，delay/repeat/onComplete，非侵入式直接操作外部变量指针。
- **TriggerZone 区域触发器**：AABB / 球两种区域，进出（边沿一次）发布 `TriggerEnterEvent` / `TriggerExitEvent`，维护 `inside` 集合。
- **全局时间缩放 + 固定时间步**：`TimeState::timeScale`（慢动作/子弹时间）、`consumeFixedSteps`（确定性物理固定步进，防死亡螺旋）。

### ECS 优化
- **组件变更事件**：`Coordinator::autoPublishComponentEvents<T>()` 开启后自动发布 `ComponentAdded<T>` / `ComponentRemoved<T>`。
- **实体列表缓存**：`Coordinator::aliveEntities()` O(1) 全量遍历（create/destroy 自动维护）。
- **渲染批处理**：RenderSystem3D 按材质纹理分组排序（同纹理连续绘制，减少描述符集切换）。
- **固定时间步**：TimeState 固定步进基础设施。

### 高层 API 打包
- **SceneFactory 场景工厂**（`ecs/scene_factory.h`，纯头文件）：一键创建相机 / 跟随相机 / 方向光 / 点光 / 立方体 / 球体 / 多面体 / 2D 圆 / 文字 / 粒子生成器 / 立方体生成器 / AABB / 球形触发器 / 碰撞盒，参数高度自定义。

### 简化开发流程
- VulkanApp 主循环自动接入：`timers::system().update(dt)` / `tween::system().update(dt)` / 帧末双事件总线 `dispatch()`。
- 3D 网格共享描述符池扩充 `COMBINED_IMAGE_SAMPLER` 容量（材质纹理）。
- 新增 `debug.vert/frag` 着色器，CMakeLists + build.json 同步注册。

### 文档
- `README.md` 精简为短开篇 + 导航（详细内容迁移至 docs/）。
- API 文档细化：04-ECS 新增 4.5~4.8（事件系统 / 逻辑系统 / ECS 优化 / SceneFactory）；06-渲染引擎新增第 07 章（DebugRenderer / 材质雾效半球光 / MSAA / 管线 samples）。
- `docs/index.html` 更新至 v1.0.2（新增条目：DebugRenderer / SceneFactory / Timer / Tween / 事件系统 / TriggerZone / Mesh3D 材质 / MSAA 等）。

---

## [1.0.1] - 2026-08

### 渲染引擎发布（功能高度集成）
在 v1.0.0 纯引擎库基础上新增 **8 个独立渲染功能模块**（`src/render/`），并装配为一条完整渲染链：

```
阴影深度 pass → 场景离屏 RT（天空盒 + 3D 网格 + 实例化 + 粒子）→ 后处理 → 主 pass → 2D/UI 叠加
```

#### 新增模块（每个均为独立 API）
- **Texture 纹理系统**（`render/texture.h/.cpp`）：GPU 纹理创建 / TGA·BMP 加载（纯 CPU 无第三方依赖）/ 棋盘·渐变·噪声程序化纹理；`RenderDevice` 共享句柄集合。
- **RenderTarget 离屏渲染**（`render/framebuffer.h/.cpp`）：离屏帧缓冲（颜色附件可采样 + 可选深度附件，深度格式可参数化），`begin/end` 切换渲染目标。
- **PostFx 后处理链**（`render/postfx.h/.cpp`）：全屏三角形后处理，7 种特效（灰度/反色/模糊/边缘/锐化/泛光/原样）+ 色调映射曝光，乒乓链式串联。
- **ShadowMap 阴影映射**（`render/shadow.h/.cpp`）：方向光深度贴图 + depth-only render pass + 深度比较采样器（PCF 基础）+ depth-bias 防自阴影。
- **Skybox 天空盒 + 环境光**（`render/skybox.h/.cpp`）：程序化天空（垂直渐变 + 太阳光斑 + 地平线暖光），无 CubeMap 依赖；环境光颜色/强度供场景光照。
- **Instancing 实例化**（`render/instancing.h/.cpp`）：每实例 mat4 + 颜色，缓冲自动扩容，单次 draw 批量渲染；8 属性顶点输入。
- **ParticleSystem 粒子**（`render/particles.h/.cpp`）：CPU 模拟（位置/速度/重力/生命周期/颜色渐变）+ 点精灵渲染 + 可选粒子纹理 + 加法混合。
- **obj_loader OBJ 加载**（`render/mesh_loader.h/.cpp`）：OBJ 解析（v/vt/vn/f、负索引、四边形三角化、缺失法线自动平面法线）。

#### 引擎集成
- `VulkanApp::createRenderModules()` 装配渲染链（失败自动回退 v1.0.0 传统直绘路径）；新增 `renderShadowPass` / `renderSceneToRT` / `beginMainPass` / `currentCameraView`。
- 渲染链视口切换统一走 `m_activeViewport/m_activeScissor`；RT 颜色/深度格式与主 pass 对齐，既有 2D/3D 管线可直接复用。
- `Mesh3D::drawDepth()` 支持阴影深度 pass（push constant 传 model）。
- `VulkanContext` 新增 `depthFormat()` 与 `framebuffer(imageIndex)` 访问器。
- `createDemoScene()` 内置渲染链演示：旋转天空盒 + 地面/悬浮体 + 120 实例立方体云 + 持续喷射粒子（可清空替换）。
- 新增 5 组着色器（postfx/shadow/skybox/instanced/particle），CMakeLists + build.json 同步注册。

#### 原有接口优化（非破坏性）
- **RenderDevice 独立**为 `render/render_device.h` 基础设施（不再定义于 texture.h），所有渲染模块共享。
- **VulkanContext::renderDevice()** 一键组装 RenderDevice，替代手工填充。
- **Shape / Mesh3D / AssetManager** 新增 `RenderDevice` 便捷重载（`upload(const RenderDevice&)` / `init(const RenderDevice&, ...)`），旧签名保留、调用点不变。
- **Instancing::setInstances** 改为实例缓冲持久映射（每帧更新零 vkMapMemory/vkUnmapMemory 开销）。
- **ParticleSystem::spawn** 除 origin 外全部参数提供默认值，支持"爆炸/火花"快捷生成。
- **VulkanApp::draw3DMeshes** 相机逻辑与渲染链统一（复用 `currentCameraView()`）。

#### 文档拆分与索引
- **API 文档拆分为 7 篇**存放于 `docs/api/`（00 总览 · 01 应用与核心 · 02 图形内容 · 03 UI · 04 ECS · 05 辅助 · 06 渲染引擎模块）。
- 新增 **`docs/index.html`** HTML 索引工具（按模块/关键字快速查找 API）。
- `README.md` 更新为 v1.0.1（渲染引擎特性 / 渲染链架构 / 目录结构 / 扩展指南）。
- 根目录 `API.md` 移除（内容迁移至 `docs/api/`）。

---

## [1.0.0] - 2026-08

### 转型发布
- **项目转型为纯引擎库**：删除全部内置演示场景与示例代码，运行后为空场景，内容完全由用户在扩展点（`registerScenes()` / `createDemoScene()`）中装配。
- **版本号重置为 1.0.0**（此前开发版本号 v1.1 ~ v1.7 不再使用，历史合并记录见下）。
- **文档重构**：
  - `API.md` 精简为纯接口参考（删除全部示例代码块，只保留接口签名与设计语义）。
  - `README.md` 更新为引擎库描述（功能特性 / 项目状态 / 扩展指南）。
  - 新增 `CHANGELOG.md`（本文件）统一维护版本历史。

### 移除（本次）
- 删除源码演示场景：`SolarSystemScene` / `FollowAndColliderScene` / `UiDemoScene`（及其 `assets/ui_demo_panel.json`）。
- 删除场景热键绑定（`scene_solar` / `scene_follow` / `scene_ui`）与延迟场景切换逻辑。
- 删除 CMake 对 `assets/` 目录的 post-build 复制。
- `API.md` 中所有「使用示例」代码块、自定义模板代码、附录 A（Cheat Sheet）。

---

## 历史版本（开发期记录，v1.x 已弃用）

### v1.7 — 渲染专业度 + 内容玩法 + UI 激活 + 工程化打磨

#### 渲染专业度（R1–R3）
- **相机宽高比修复**：窗口缩放后 view/proj 与视口同步更新，画面不再拉伸变形。
- **3D 深度缓冲 + 深度测试**：renderPass 增加深度附件，3D 网格按深度正确遮挡。
- **顶点法线 + N·L 光照**：Mesh3D 顶点布局 6→9（pos + normal + color），mesh3d shader 接入朗伯漫反射（N·L）；LightingSystem 主方向光注入。

#### 内容与玩法（C1–C3）
- **可播种随机数**：`Rng` 工具替换 `std::rand`，同种子可复现场景。
- **Spawner 增强**：3D 立方体粒子 + 颜色/速度/重力 + 生命周期。
- **场景多样化**：新增金星/火星/星空背景/喷泉/地面/立柱等实体。

#### UI 系统激活（U1–U3）
- **新控件**：UiSlider 滑块 + UiCheckbox 复选框；修复 UiText 文字颜色（`text_color` 独立于背景色）。
- **JSON 事件回调**：`UiBindings` 类型化绑定表对接 JSON `on_xxx` 字段，无需 C++ 中二次查找。
- **UI 演示场景**：F3 进入（后续随 1.0.0 移除），JSON 驱动可拖拽面板 + 延迟场景切换。

#### 工程化打磨（E1–E4）
- **Profiler 系统级接入**：System 基类注入 Profiler*，10 个逻辑系统 PROFILE_SCOPE 计时；DebugSystem 左上角多行上屏。
- **系统调度扩展**：启停（`setEnabled`/`setEnabledByName`）、优先级（`setPriority`/`setPriorityByName`，稳定排序）、重复注册检查。
- **碰撞三态事件**：`CollisionEnterEvent` / `CollisionEvent` / `CollisionExitEvent` 边沿触发 + **双向位置修正**（按 isStatic 分配穿透量）。
- **配置解析统一**：config.json 改用 `ui_json` 的 `parseJson`（JSONC 注释/尾随逗号），移除正则解析。
- **死代码清理**：删除 `Shape::draw` / `Mesh3D::draw` / `Mesh3D::markUboDirty`。
- **稳定性修复**：析构顺序 bug（销毁描述符池前先 `flushAllDeferredDestroy`）；描述符集延迟销毁（防池耗尽）。

### v1.6 — ECS 架构扩展 + GPU 资源安全

#### 核心架构
- **Scene/SceneManager**：多场景管理，onEnter/onExit/onUpdate 生命周期回调，switchTo 热切换。
- **Profiler**：帧级 + 系统级性能采样，frameBegin/frameEnd + PROFILE_SCOPE RAII 计时宏，0.5s 窗口快照。
- **InputMapper**：语义动作映射，justPressed/justReleased 边沿检测，运行时 rebind 自定义按键。
- **EventBus 集成**：Coordinator 内置事件总线，CollisionEvent 碰撞事件由 ColliderSystem 发布。

#### 新组件 + 新系统
- **Light / LightingSystem**：方向光/点光/聚光，isPrimary 主光收集。
- **Collider / ColliderSystem**：AABB 碰撞检测，O(n²) 双循环，重叠时 publish CollisionEvent + 位置修正。
- **Follow / CameraSystem 扩展**：相机平滑跟随目标（lerp 插值），lookAt 朝向目标。

#### 内置演示场景（随 1.0.0 移除）
- SolarSystemScene（F1）：四级层级 + 玩家可控 + 粒子生成 + 主光 + 碰撞盒。
- FollowAndColliderScene（F2）：相机跟随 + AABB 碰撞 + 障碍物阵列。

#### EnTT 3.13 API 兼容
- `entityCount()` / `clear()` / `eachEntity()` 适配 `storage<Entity>()->size()` 与 `begin()/end()` 迭代。

#### GPU 资源延迟销毁（修复 DEVICE_LOST）
- 引入 2 槽环形延迟销毁队列：`deferDestroyBuffer()` / `flushDeferredDestroy()` / `flushAllDeferredDestroy()`。
- 彻底修复「ECS 更新阶段销毁 GPU 缓冲 → 上一帧命令缓冲仍在执行 → use-after-free → DEVICE_LOST」。

### v1.2 — 字体系统 + 代码构建 UI

- **Font 抽象层**：`Font` / `PixelFont` / `SmoothFont` / `FontRegistry`。SmoothFont 用 GDI `GetGlyphOutlineA(GGO_GRAY8_BITMAP)` 光栅化任意 TTF 系统字体。
- **TextRenderer 多字体支持**：`setFont(Font*)` 运行时切换像素字/平滑字体。
- **代码构建 UI**：`UiBuilder` 流式 API；`UiManager::setRoot()` + `UiElement::addChild()` 完全代码构建。
- **UI 控件新访问器**：三态颜色 / `setDraggable` / `setFont`。

#### 平滑字体抗锯齿原理
- GDI 灰度 0~64 归一化到 0~255；顶点色 RGB = `textColor * alpha/255`，每像素 alpha 调制进亮度，实现灰度抗锯齿（不改 shader、不开 alpha blend）。

### v1.1 — 稳定性修复与性能优化

#### 修复
- `drawText` 用错 pipeline → 改用 `m_pipe2D` + `drawVBOOnly`。
- `draw3DMeshes` view/proj 不更新 UBO → `Mesh3D::drawVBOOnly` 缓存 `m_lastView/m_lastProj` 检测变化。
- `Mesh3D::upload` 设备变更销毁用错设备 → 用旧 `m_device` 销毁旧缓冲。
- `uploadToBuffer` 不检查 `vkMapMemory` 返回值 → 加检查 + 空操作保护。
- JSON 解析器不支持注释 → `skipWs` 支持 `//` 行注释和 `/* */` 块注释。

#### 性能优化
- Mesh3D UBO 持久映射（`m_uboMapped`）。
- drawText / UI 元素改用 `drawVBOOnly`。
- draw2DShapes 跳过空组。

---

[1.0.0]: https://github.com/<your-user>/VulkanProjectBase/releases/tag/1.0.0
