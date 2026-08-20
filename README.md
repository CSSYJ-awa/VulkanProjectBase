# VulkanProjectBase —— 基于 Vulkan + MinGW 的 2D/3D 图形引擎框架

> 一个零冗余、易于扩展的 C++ 图形学教学/起步工程。  
> 内置：**Vulkan 渲染管线** · **2D 图元系统** · **3D 网格系统** · **位图文字渲染** · **JSON 驱动 UI 系统** · **CMake 环境诊断**

---

<p align="center">
  <img src="https://img.shields.io/badge/C++-17-blue?logo=c%2B%2B&style=flat-square" />
  <img src="https://img.shields.io/badge/Vulkan-1.4-AC162C?logo=vulkan&style=flat-square" />
  <img src="https://img.shields.io/badge/GLFW-3.x-183A6C?logo=opengl&style=flat-square" />
  <img src="https://img.shields.io/badge/GLM-1.0.x-007bff?style=flat-square" />
  <img src="https://img.shields.io/badge/CMake-3.15+-green?logo=cmake&style=flat-square" />
  <img src="https://img.shields.io/badge/Compiler-MinGW%20GCC%20≥8-important?style=flat-square" />
  <img src="https://img.shields.io/badge/Platform-Windows-lightgray?logo=windows&style=flat-square" />
  <img src="https://img.shields.io/badge/License-MIT-yellowgreen?style=flat-square" />
</p>

---

## ✨ 功能特性

### 渲染核心
- **纯 Vulkan 1.x 渲染**：交换链 · 命令缓冲 · 多帧同步（2 帧飞行）
- **窗口自适应**：支持 `stretch`（全屏拉伸）与 `letterbox`（保持比例 + 黑边）两种模式
- **多条图形管线**：Pipeline2D（填充/线段/连续线段/三角扇）· Pipeline3D · PipelineText

### 2D 图元系统（`Shape` 抽象基类）
| 图元 | 拓扑 | 说明 |
|------|------|------|
| `Line` | LINE_LIST | 线段（两点） |
| `Triangle` | TRIANGLE_LIST | 三角形（三点） |
| `Rectangle` / `Square` | TRIANGLE_LIST | 矩形 / 正方形 |
| `Circle` | **TRIANGLE_FAN** ⚡ | 圆（默认 32 段，顶点数 ↓67%） |
| `Wave` | LINE_STRIP | 正弦波形 |
| `Polygon` | **TRIANGLE_FAN** ⚡ | 凸多边形（FAN 三角化） |

### 3D 网格系统（`Mesh3D` 抽象基类）
| 网格 | 说明 |
|------|------|
| `Cube` | 立方体（6 面，每面 2 三角） |
| `Polyhedron` | 多面体 · Tetrahedron（4）· Cube（6）· Octahedron（8）· Icosahedron（20）· Dodecahedron（12） |

UBO 脏标志智能更新：仅模型矩阵变化时重新映射内存，避免每帧 vkMapMemory 压力。

### 文字渲染
- 位图字体（内置 6x8 ASCII 点阵，128 字符）
- 像素坐标锚定，无需投影矩阵

### UI 系统（JSON 驱动，多继承 Mix-in 交互）
```
UiElement (single base)
├── UiText        : UiElement                              (单继承 — 纯文本)
├── UiButton      : UiElement + IClickable + IHoverable    (多继承 — 按钮)
├── UiTextBox     : UiText    + ITextInput                 (链式多继承 — 文本输入框)
└── UiPanel       : UiElement + IDraggable                 (多继承 — 可拖拽面板)
```

支持：全局焦点管理、UTF-8 安全退格/删除、回车/ESC 失焦、点击空白清空焦点。

### 工程质量保障
| 模块 | 说明 |
|------|------|
| `CheckEnvironment.cmake` | CMake 配置阶段**自动诊断** 5 项环境依赖，FAIL 给出修复建议 |
| `GenerateVulkanMingwLib.cmake` | MinGW 编译器自动从 System32 `vulkan-1.dll` 生成 `libvulkan-1.dll.a`，解决 Vulkan SDK 只带 MSVC `.lib` 的痛点 |
| `FindGLFW_MSYS2.cmake` / `FindGLM_MSYS2.cmake` | MSYS2 自动探测；GLFW **三层回退策略**：find_package → MSYS2 → find_library |
| `setup_env.ps1` | 一键安装：Vulkan SDK · MSYS2 UCRT64 · GLFW · GLM |

---

## 🎯 截图预览

```
┌──────────────────────────────────────────────────────────────┐
│ Graphics Engine (Vulkan + MinGW)  · 1280×720 · letterbox    │
├───────────────────────────────────────────────┬──────────────┤
│                                               │ UI DEMO PANEL│ ← 可拖拽
│  2D 区：矩形/圆/三角形/多边形/波形             │ [HELLO][QUIT]│
│                                               │ TYPE BELOW:  │
│  3D 区：立方体 + 八面体 + 二十面体             │ ┌──────────┐ │
│          (绕 Y 轴持续旋转)                    │ │text input│ │ ← 支持退格
│                                               │ └──────────┘ │
│                                               │ ESC TO EXIT  │
└───────────────────────────────────────────────┴──────────────┘
         GRAPHICS ENGINE (文字)                                     
```

---

## 📦 环境要求

> **CMake 配置阶段会自动检测**，任何一项不满足都会给出可直接复制粘贴的修复命令。

| 依赖 | 最低版本 | 安装方式（Windows） |
|------|---------|-------------------|
| **Windows** | 10/11 x64 | — |
| **编译器** | MinGW GCC ≥ 8 | [MSYS2 UCRT64](https://www.msys2.org/) `pacman -S mingw-w64-ucrt-x86_64-toolchain` |
| **CMake** | ≥ 3.15 | [官网](https://cmake.org/download/) 或 `winget install Kitware.CMake` |
| **Vulkan SDK** | 任意（推荐 ≥ 1.3） | [LunarG SDK](https://vulkan.lunarg.com/) · 必须设置 `VULKAN_SDK` 环境变量 |
| **GLFW** | 3.x | `pacman -S mingw-w64-ucrt-x86_64-glfw`（静态链接优先） |
| **GLM** | ≥ 0.9.9 | `pacman -S mingw-w64-ucrt-x86_64-glm`（纯头文件） |
| **图形驱动** | — | NVIDIA / AMD / Intel · 必须支持 Vulkan 1.x |

### 一键环境脚本（PowerShell）
```powershell
# 管理员权限
Set-ExecutionPolicy Bypass -Scope Process -Force
.\scripts\setup_env.ps1
```

---

## 🚀 快速开始

### 1) 克隆并进入项目
```bash
git clone https://github.com/<your-user>/VulkanProjectBase.git
cd VulkanProjectBase
```

### 2) CMake Configure（MinGW Makefiles）
使用 VSCode Ctrl+Shift+P → **Tasks: Run Task** → `CMake: configure (shell fallback)`  
或命令行：
```powershell
cmake -S . -B build -G "MinGW Makefiles" `
    -DCMAKE_C_COMPILER=gcc.exe `
    -DCMAKE_CXX_COMPILER=g++.exe `
    -DCMAKE_BUILD_TYPE=Debug
```

如果环境正常，你会看到诊断报告（示例）：
```
╔══════════════════════════════════════════════════╗
║              环境诊断结果汇总                    ║
╠══════════════════════════════════════════════════╣
║  总计检测 5 项                                    ║
║  通过: 5  [OK]                                   ║
║  失败: 0                                         ║
╠══════════════════════════════════════════════════╣
║  所有检测通过！环境就绪，可以构建。              ║
╚══════════════════════════════════════════════════╝
```

### 3) Build
使用 VSCode **Ctrl+Shift+B** 或：
```powershell
cmake --build build --config Debug -j 8
```

构建完成后产物在 `bin/` 目录：
```
bin/
├── GraphicsEngine.exe   # 从 config.json 的 exe_name 读取
├── config.json          # （post-build 自动复制）
├── ui_config.json       # （post-build 自动复制）
└── shaders/
    ├── basic.vert.spv / basic.frag.spv     # 2D Pipeline
    ├── mesh3d.vert.spv / mesh3d.frag.spv   # 3D Pipeline
    └── text.vert.spv / text.frag.spv       # 文字 Pipeline
```

### 4) 运行
```powershell
cd bin
.\GraphicsEngine.exe
```

> **退出**：按 `ESC` 或关闭窗口  
> **交互**：拖动右上 UI 面板位置；点击文本框输入字符，按 `Backspace/Delete` 删除，`ESC` 失焦；点击 `QUIT` 按钮或按 `ESC` 退出。

---

## 📁 项目目录结构

```
VulkanProjectBase/
├── CMakeLists.txt                  # 顶层构建：环境诊断 + 着色器编译 + post-build 部署
├── config.json                     # ⭐ 驱动构建（exe_name/sources/extra_libs）+ 运行时窗口配置
├── ui_config.json                  # ⭐ JSON 驱动 UI 面板（可拖拽 + 按钮 + 文本 + 输入框）
├── LICENSE                         # MIT License
│
├── cmake/                          # CMake 模块
│   ├── CheckEnvironment.cmake      # 配置阶段自动环境诊断（5项全通过才继续）
│   ├── GenerateVulkanMingwLib.cmake# MinGW 自动生成 Vulkan 导入库
│   ├── FindGLFW_MSYS2.cmake        # GLFW MSYS2 探测 + 三层回退 + 静态优先
│   ├── FindGLM_MSYS2.cmake         # GLM MSYS2 探测
│   └── FindMSYS2.cmake
│
├── scripts/
│   └── setup_env.ps1               # 一键部署 Vulkan SDK / MSYS2 / GLFW / GLM
│
└── src/
    ├── main.cpp                    # 入口 + Windows 控制台 UTF-8 代码页修复（setlocale 多级回退）
    ├── vulkan_app.h / .cpp         # 主应用：配置加载 · 引擎创建 · 场景构建 · 主循环 · 回调
    │
    ├── engine/                     # Vulkan 核心封装
    │   ├── vulkan_context.h/.cpp   # Instance · Surface · 设备 · 交换链 · 命令池 · 同步对象
    │   ├── vulkan_util.h/.cpp      # 缓冲创建/上传/销毁 + 工具函数
    │   └── pipelines.h/.cpp        # Pipeline2D · Pipeline3D · PipelineText 构建
    │
    ├── shapes/                     # 2D 图元系统
    │   └── shape.h/.cpp            # Shape 基类 + Line/Triangle/Rectangle/Circle/Wave/Polygon
    │
    ├── geometry3d/                 # 3D 网格系统
    │   └── mesh3d.h/.cpp           # Mesh3D 基类 + Cube + Polyhedron（5种）
    │
    ├── text/                       # 文字渲染系统
    │   ├── bitmap_font.h/.cpp      # 6x8 内置 ASCII 点阵（128字符）
    │   └── text_renderer.h/.cpp    # TextRenderer（继承自 Shape，像素→NDC 转换）
    │
    ├── ui/                         # UI 系统（JSON 驱动 + Mix-in 多继承交互）
    │   ├── ui_element.h            # UiElement 基类：树形结构 + 命中测试 + 脏标志渲染
    │   ├── ui_event.h              # UiMouseEvent / UiKeyEvent + IClickable/IHoverable/IDraggable/ITextInput
    │   ├── ui_widgets.h/.cpp       # UiText + UiButton + UiTextBox + UiPanel 具体实现
    │   ├── ui_manager.h/.cpp       # UiManager：根节点持有 + 输入分发（Hover/Drag/Key/Char）
    │   ├── ui_loader.h/.cpp        # UI JSON 文件加载器（递归构建 UI 树）
    │   └── ui_json.h/.cpp          # 轻量 JSON 解析（无第三方依赖）
    │
    └── shaders/                    # GLSL 着色器（编译期 → SPIR-V）
        ├── basic.vert / basic.frag     # 2D 图元（每顶点 xyrgb）
        ├── mesh3d.vert / mesh3d.frag   # 3D 网格（每顶点 xyzrgb，PushConst 颜色 + UBO MVP）
        └── text.vert / text.frag       # 文字（点阵字体采样）
```

---

## ⚙️ 配置说明

### `config.json`（构建 + 运行时双驱动）
```jsonc
{
    // 【构建期】决定 exe 输出文件名
    "exe_name": "GraphicsEngine",

    // 【运行时】窗口标题/尺寸（VulkanApp 启动时读取）
    "window_title": "Graphics Engine (Vulkan + MinGW)",
    "window_width": 1280,
    "window_height": 720,

    // 【运行时】UI 配置 JSON 路径
    "ui_config": "ui_config.json",

    // 【运行时】画面比例模式
    //   "stretch"   → 画面随窗口拉伸（不保持比例）
    //   "letterbox" → 保持初始宽高比，超出部分填充清除色（黑边）
    "aspect_mode": "letterbox",

    // 【构建期】编译源文件列表（⚠️ 只放 .cpp，不要 .h）
    "sources": [ ... ],

    // 【构建期】额外链接库（如 physfs / freetype 等）
    "extra_libraries": []
}
```

### `ui_config.json`（JSON 驱动 UI）
```jsonc
{
    "ui": {
        "type": "panel",
        "name": "main_panel",
        "x": 880, "y": 40, "width": 380, "height": 640,
        "draggable": true,
        "color": [0.12, 0.12, 0.18, 0.85],   // r,g,b,a  |  alpha=0 透明继承背景
        "children": [
            { "type": "text",    ... },  // 纯文字
            { "type": "button",  ... },  // 按钮（可点击/悬停变色）
            { "type": "textbox", ... },  // 文本输入框
            { "type": "panel",   ... }   // 递归子面板（支持无限层级）
        ]
    }
}
```

在 [vulkan_app.cpp:L119-L135](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/vulkan_app.cpp#L119) 中为按钮绑定回调示例：
```cpp
if (auto* btn = m_ui->root()->findByName("btn_quit")) {
    if (auto* b = dynamic_cast<UiButton*>(btn)) {
        b->setClickHandler([this]() { /* 代码 */ });
    }
}
```

---

## 🏗️ 架构设计

### 资源管理（性能关键）

```
                    ┌────────────── 脏标志（m_dirty）
                    │  仅数据变化时重建 CPU 顶点
                    ▼
用户修改属性 → generateVertices() ─┐
                                    ├─▶ upload()：仅设备或大小变化时重建 GPU 缓冲，
                                    │                否则仅上传数据 ✅ 避免 GPU UAF
                                    ▼
                            draw() / drawVBOOnly()
                                    │
                                    ▼
                          按拓扑分组，每组只
                          BindPipeline/Viewport/Scissor 一次
                          （详见 draw2DShapes / draw3DMeshes）
```

### 同步策略（窗口缩放安全）
- `MAX_FRAMES_IN_FLIGHT = 2`（双缓冲飞行）
- `recreateSwapChain()` 时：**等待 GPU 空闲** → 重建 `m_imagesInFlight`（resize 到 swapchain 图片数量，清零 entries）→ 重建 Framebuffer/Viewport
- Mesh3D/Shape 的 `upload()`：**设备变更或大小变化才 destroy/create**，否则复用旧缓冲（避免 destroy 后仍被上一帧使用导致 `VK_ERROR_DEVICE_LOST`）

### UI 多继承 Mix-in 架构
每种交互能力是独立类（IClickable / IHoverable / IDraggable / ITextInput），控件按需多继承组合，避免接口污染。见 [ui_event.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_event.h)。

### 描述符池生命周期
- `Pipeline3D` 的 3D 网格共享描述符池：由 `VulkanApp::m_meshDescPool` 统一持有，析构时在 mesh 清空后销毁（描述符集从池分配 → 先 mesh 后池）
- `PipelineText` 使用 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`，不通用 3D 的 UBO 池

---

## 🛠️ 自定义扩展指南

### 新增 2D 图元
```cpp
// shapes/my_shape.h
#include "shape.h"
class MyShape : public Shape {
public:
    MyShape(...) { /* 初始化参数，可选 m_lineTopology/m_fanTopology = true */ }
protected:
    void generateVertices() override {
        m_vertices.clear();
        // 填充 m_vertices：每顶点 5 个 float (x, y, r, g, b)
        // Vulkan NDC: x∈[-1,1], y∈[-1,1]（Y 向下，与 OpenGL 相反）
    }
};
```
然后在 [vulkan_app.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/vulkan_app.cpp) 的 `createDemoScene()` 中 `std::make_unique<MyShape>(...)` + `uploadShape()` + `m_shapes.push_back()`。

### 新增 3D 网格
```cpp
// geometry3d/my_mesh.h
#include "mesh3d.h"
class MyMesh : public Mesh3D {
public:
    MyMesh(/* params */) : /* init */ {}
protected:
    void generateVertices() override {
        m_vertices.clear();
        // 填充 m_vertices：每顶点 6 个 float (x, y, z, r, g, b)
        // 采用 TRIANGLE_LIST（Cube 每面 2 三角）
    }
};
```

### 新增 UI 控件
```cpp
// ui/ui_custom.h
#include "ui_widgets.h"
class UiMyWidget : public UiElement, public IClickable {
public:
    void onClick() override { /* 自定义点击行为 */ }
protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
    void drawSelf(const UiRenderContext& ctx) override; // 自定义绘制
};
```
并在 [ui/ui_loader.cpp](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/ui/ui_loader.cpp) 的类型注册表中 `"my_widget"` 映射到 `std::make_unique<UiMyWidget>()`。

---

## ⚠️ 常见问题 FAQ

### Q1. 启动报 `STATUS_DLL_NOT_FOUND`
**原因**：GLFW 被动态链接但 `glfw3.dll` 不在 PATH。  
**修复**：工程已优先静态链接（`libglfw3.a`），重新运行 CMake Configure 使其探测到静态库（输出 `[GLFW MSYS2] 找到 静态库: .../libglfw3.a`）。

### Q2. 窗口拉伸/缩放导致崩溃 `vkQueueSubmit` 失败
**原因**：旧版 `m_imagesInFlight` 大小不匹配新 swapchain 图片数量（已修复 ✅）。  
**修复**：使用最新代码；若仍发生，检查 `VulkanContext::recreateSwapChain()` 里 `m_imagesInFlight.assign(size, VK_NULL_HANDLE)` 是否存在。

### Q3. UI 元素上下颠倒
**原因**：Vulkan NDC 的 Y 轴向下（与 OpenGL 相反）。旧版 `pixelToNdc` 翻转错了（已修复 ✅）。  
**修复**：确认 [shape.h](file:///d:/CSSYJ/Projects/VulkanProjectBase/src/shapes/shape.h#L80-L81) 中 `ny = 2*py/h - 1`（不额外取反）。

### Q4. 控制台中文乱码
**原因**：Windows 控制台默认代码页 936（GBK），源码 UTF-8。  
**修复**：程序已在入口自动设置 `SetConsoleOutputCP(CP_UTF8)` + `locale` 多级回退；若仍乱码，升级 Windows Terminal ≥ 1.18（原生 UTF-8 支持）。

### Q5. `No Vulkan implementation found`
**原因**：显卡驱动或 Vulkan Runtime 没安装。  
**修复**：到 GPU 厂商官网下载最新驱动（含 Vulkan ICD）；或安装 [Vulkan Runtime](https://vulkan.lunarg.com/sdk/home#windows)。

### Q6. 构建时报 `undefined reference to ChoosePixelFormat@...`
**原因**：GLFW 静态库依赖 Win32 GDI/OpenGL 系统库。  
**修复**：工程在 [CMakeLists.txt:L359-366](file:///d:/CSSYJ/Projects/VulkanProjectBase/CMakeLists.txt#L359) 已经加入 `gdi32 opengl32 m`，不要手动删除；确保链接顺序正确。

### Q7. 编译错误 `Rectangle / Polygon 不是类名`
**原因**：`windows.h` 的 GDI 宏污染。  
**修复**：工程在 [CMakeLists.txt:L374-379](file:///d:/CSSYJ/Projects/VulkanProjectBase/CMakeLists.txt#L374) 已经加入 `WIN32_LEAN_AND_MEAN` 和 `NOGDI` 编译定义，不要手动删除。

---

## 📈 性能数据（Debug 构建，i7-13700K + RTX 4070）

| 指标 | 原版本 | 优化后 | 提升 |
|------|--------|--------|------|
| Circle(32段) 顶点数 | 96 | 34 | ↓65% |
| Polygon(8边) 顶点数 | 18 | 9 | ↓50% |
| draw2DShapes 命令数（8图元） | 32 条 Bind/State | 16 条 ← 4 组只设一次公共状态 | ↓50% |
| draw3DMeshes 命令数（3 mesh） | 15 条 | 9 条 | ↓40% |
| Mesh3D UBO CPU→GPU 拷贝频率 | 每帧×3 | setModel 后 1 帧×3，后续 0 次 | 静态场景 ↓100% |

---

## 🧪 已验证用例

- ✅ 启动运行 30 秒无错误（Validation Layers 无 ERROR/WARNING）
- ✅ 水平→垂直→对角线窗口拖拽缩放（连续 60 秒，无 swapchain 崩溃）
- ✅ UI 面板拖拽（子元素位置跟随，无残留）
- ✅ 文本框：输入英文 → 退格 → Delete → 多文本框焦点切换 → 点击空白失焦
- ✅ `aspect_mode: "stretch"` / `"letterbox"` / 非法值 `"xxx"`（回退+警告）
- ✅ 3D 网格绕 Y 轴持续旋转（UBO 脏标志生效）
- ✅ 控制台中文输出正确（"[Config] 已加载" / "图形引擎初始化完成"）

---

## 📜 License

MIT License © 2026 CSSYJ-awa  
详见 [LICENSE](LICENSE)。
