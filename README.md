# VulkanProjectBase —— 基于 Vulkan + MinGW 的 2D/3D 图形渲染引擎

> 纯引擎库（不内置演示场景），零冗余、易于扩展的 C++ 图形引擎。
> 内置：**Vulkan 渲染管线** · **2D 图元** · **3D 网格** · **文字渲染** · **JSON UI** · **ECS** · **场景管理** · **性能分析** · **渲染引擎 v1.0.1** · **逻辑系统 v1.0.2**

<p align="center">
  <img src="https://img.shields.io/badge/version-1.0.2-blue?style=flat-square" />
  <img src="https://img.shields.io/badge/C++-17-blue?logo=c%2B%2B&style=flat-square" />
  <img src="https://img.shields.io/badge/Vulkan-1.4-AC162C?logo=vulkan&style=flat-square" />
  <img src="https://img.shields.io/badge/GLFW-3.x-183A6C?style=flat-square" />
  <img src="https://img.shields.io/badge/GLM-1.0.x-007bff?style=flat-square" />
  <img src="https://img.shields.io/badge/CMake-3.15+-green?style=flat-square" />
  <img src="https://img.shields.io/badge/EnTT-3.13.2-009639?style=flat-square" />
  <img src="https://img.shields.io/badge/Platform-Windows-lightgray?style=flat-square" />
  <img src="https://img.shields.io/badge/License-MIT-yellowgreen?style=flat-square" />
</p>

---

## 📑 导航

| 你想… | 去哪里 |
|------|--------|
| **查 API** | ⭐ [docs/index.html](docs/index.html)（索引工具）· [docs/api/00-overview.md](docs/api/00-overview.md) |
| 跑起来 | [快速开始](#-快速开始) · [环境要求](#-环境要求) |
| 改配置 | [配置](#️-配置) |
| 看更新 | [CHANGELOG.md](CHANGELOG.md) |

---

## ✨ 功能一览

### ⭐ 渲染引擎（`src/render/`，独立 API 模块）
```
阴影 pass → 场景离屏 RT(MSAA) → 后处理 → 主 pass → 2D/UI 叠加
```
- **v1.0.1**：Texture 纹理 · RenderTarget 离屏 · PostFx 后处理（7 特效）· ShadowMap 阴影 · Skybox 天空盒 · Instancing 实例化 · ParticleSystem 粒子 · OBJ 加载
- **v1.0.2**：DebugRenderer 调试可视化 · Mesh3D 材质（三平面映射）/距离雾/半球环境光 · **MSAA 多重采样**（config `msaa_samples`，1/2/4/8）

### 🧠 逻辑系统（v1.0.2，可复用 API）
- **完整事件系统** `events::EventBus`：优先级订阅 · 一次性监听 · 延迟派发队列 · 全局单例
- **TimerSystem** 定时器 · **TweenSystem** 缓动动画（8 曲线，非侵入式）· **TriggerZone** 区域触发器 · **TimeState** 全局时间缩放 + 固定时间步

### ⚙️ ECS 实体组件系统
- Coordinator + EntityBuilder 链式建实体 · 14 组件 · 13 系统（Movement/Camera/Collider 三态事件/Trigger/Hierarchy/Lighting/Spawner/Debug…）
- **v1.0.2**：组件变更事件（ComponentAdded/Removed）· 实体列表缓存 · 渲染批处理（按纹理分组）· **SceneFactory** 场景工厂（一键创建相机/灯光/网格/触发器）

### 🖥️ 基础能力
- 2D 图元（FAN 拓扑省 67% 顶点）· 3D 网格（N·L 光照 + UBO 脏标志）· 位图/平滑字体 · JSON 驱动 UI · 多场景 · Profiler · InputMapper · AssetManager · Prefab

---

## 🚀 快速开始

```powershell
# 1. 环境（Vulkan SDK + MSYS2 UCRT64 + GLFW + GLM）
.\scripts\setup_env.ps1        # 管理员 PowerShell

# 2. 配置 + 构建
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug -j 8

# 3. 运行
cd bin
.\GraphicsEngine.exe           # ESC 退出
```

产物在 `bin/`：`GraphicsEngine.exe` + `config.json` + `shaders/*.spv`（post-build 自动部署）。

## 📦 环境要求

| 依赖 | 最低版本 | 安装 |
|------|---------|------|
| Windows 10/11 x64 · MinGW GCC ≥ 8 | MSYS2 UCRT64 | `pacman -S mingw-w64-ucrt-x86_64-toolchain` |
| CMake ≥ 3.15 · Vulkan SDK（`VULKAN_SDK` 已设）· GLFW 3.x · GLM ≥ 0.9.9 | — | `setup_env.ps1` 一键部署 |

> CMake 配置阶段自动诊断环境，不满足会给出可直接执行的修复命令。

## ⚙️ 配置

**`build.json`**（构建期）：`exe_name` / `sources`（只列 .cpp）/ `extra_libraries`。

**`config.json`**（运行时，JSONC 注释/尾随逗号均可）：
```jsonc
{
    "window_title": "Graphics Engine",
    "window_width": 1280,
    "window_height": 720,
    "aspect_mode": "stretch",      // stretch=拉伸 / letterbox=保持比例
    "msaa_samples": 4              // MSAA：1 / 2 / 4 / 8
}
```

## 📁 目录结构（要点）

```
src/
├── main.cpp / vulkan_app.h/.cpp   # 入口 + 顶层应用（配置/渲染链/ECS/场景/主循环）
├── ecs/                           # Coordinator · 组件 · 系统 · EventBus · Scene · Profiler · SceneFactory(v1.0.2)
├── engine/                        # VulkanContext · vulkan_util · pipelines · event_system · timers · tween(v1.0.2)
├── render/                        # 渲染引擎：Texture · RenderTarget(MSAA) · PostFx · Skybox · Shadow · Instancing · Particles · DebugRenderer · OBJ
├── shapes/ · geometry3d/ · text/  # 2D 图元 · 3D 网格 · 文字/字体
├── ui/                            # JSON 驱动 UI（UiManager/Builder/Loader）
└── shaders/                       # GLSL → SPIR-V（编译期）
```

## 🏗️ 架构要点

- **渲染链**：`beginFrame → 阴影 → 场景RT(MSAA) → 后处理 → 主pass → 2D/UI`；模块任一创建失败自动回退传统直绘
- **GPU 安全**：双帧飞行 + 延迟销毁队列（防 DEVICE_LOST）；Mesh3D/Shape 上传仅设备或尺寸变化才重建缓冲
- **ECS**：EnTT 封装 + 系统有序调度（优先级/启停）+ 事件总线解耦；每帧末统一派发排队事件
- **开发流**：SceneFactory 一键建实体 · EventBus 跨模块通信 · Timer/Tween 主循环自动驱动

## 🔧 自定义扩展

| 想扩展什么 | 做法 |
|-----------|------|
| 新 2D 图元 / 3D 网格 | 继承 `Shape` / `Mesh3D`，实现 `generateVertices()` |
| 新 ECS 组件 / 系统 | `components.h` 加纯数据 struct；继承 `System` 并在 `registerEcsSystems()` 注册 |
| 新场景 | 继承 `Scene`，`registerScenes()` 中 `add<MyScene>()` |
| 新 UI 控件 | 继承 `UiElement`（+ Mix-in 接口） |
| 订阅事件 / 用渲染模块 | `m_ecs->events().subscribe<E>(cb)`；`render/` 各模块均为独立 API |

> 全部接口签名与约束详见 **⭐ [docs/index.html](docs/index.html)**。

## 📜 文档与版本

- [CHANGELOG.md](CHANGELOG.md) — v1.0.2（渲染高级 / 逻辑系统 / ECS 优化 / SceneFactory）· v1.0.1（渲染引擎 8 模块）
- **MIT License** © 2026 CSSYJ-awa
