# VulkanProjectBase API 文档 —— 总览与导航

> 渲染引擎版本 **v1.0.1** (2026-08) · 本文档只描述**接口签名与设计语义**，不含示例代码。
> 快速导航请使用 [docs/index.html](../index.html)（可搜索索引工具）。
> 快速上手见 [README.md](../../README.md)，版本历史见 [CHANGELOG.md](../../CHANGELOG.md)。

## 版本说明

**v1.0.1 渲染引擎发布**：在 v1.0.0 纯引擎库基础上，新增 8 个独立渲染功能模块
（`src/render/`），并装配为一条完整渲染链：

```
阴影深度 pass → 场景离屏 RT（天空盒 + 3D 网格 + 实例化 + 粒子）→ 后处理 → 主 pass → 2D/UI 叠加
```

| 新模块 | 文件 | 功能 |
|--------|------|------|
| Texture 纹理系统 | `render/texture.h/.cpp` | GPU 纹理创建 / TGA·BMP 加载 / 程序化纹理 |
| RenderTarget 离屏渲染 | `render/framebuffer.h/.cpp` | 离屏帧缓冲（颜色 + 深度，可采样） |
| PostFx 后处理链 | `render/postfx.h/.cpp` | 7 种特效 + 色调映射 |
| ShadowMap 阴影映射 | `render/shadow.h/.cpp` | 方向光深度贴图 + PCF 采样器 |
| Skybox 天空盒 + 环境光 | `render/skybox.h/.cpp` | 程序化天空渐变 + 太阳光斑 + 环境光参数 |
| Instancing 实例化 | `render/instancing.h/.cpp` | 每实例 mat4 + 颜色批量绘制 |
| ParticleSystem 粒子 | `render/particles.h/.cpp` | CPU 模拟点精灵 + 纹理粒子 |
| obj_loader OBJ 加载 | `render/mesh_loader.h/.cpp` | OBJ 解析（v/vt/vn/f，负索引，四边形） |

## 文档索引

### 渲染核心（引擎层）
| 文档 | 内容 |
|------|------|
| [01 应用层与渲染核心](01-app-core.md) | VulkanApp（含 v1.0.1 渲染链装配）· VulkanContext · vulkan_util · 图形管线 |

### 图形内容（2D / 3D / 文字 / UI）
| 文档 | 内容 |
|------|------|
| [02 图形内容系统](02-content.md) | Shape 2D 图元 · Mesh3D 网格 · 文字渲染 · 字体系统 |
| [03 UI 系统](03-ui.md) | UiElement 树 · Mix-in 事件 · 6 控件 · UiBuilder · UiManager/UiLoader |

### ECS 实体组件系统
| 文档 | 内容 |
|------|------|
| [04 ECS 系统](04-ecs.md) | Coordinator · 14 组件 · 12 系统 · EventBus |

### 辅助模块
| 文档 | 内容 |
|------|------|
| [05 辅助模块](05-aux.md) | Scene 场景管理 · Profiler 性能分析 · InputMapper · AssetManager · PrefabRegistry · 调试附录 |

### ⭐ 渲染引擎模块（v1.0.1 新增）
| 文档 | 内容 |
|------|------|
| [06 渲染引擎模块](06-render-engine.md) | RenderDevice · Texture · RenderTarget · PostFx · Skybox · ShadowMap · Instancing · ParticleSystem · obj_loader |

> **阅读建议**：新用户从 01 → 02 → 03 → 04 按引擎依赖顺序阅读；
> 使用渲染引擎高级功能直接看 06；场景/性能等辅助模块按需查阅 05。

---

维护：CSSYJ-awa · MIT License
