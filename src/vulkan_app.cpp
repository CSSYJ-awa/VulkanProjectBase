/**
 * VulkanApp 实现 —— 图像引擎顶层
 */
#include "vulkan_app.h"

#include "engine/logger.h"
#include "engine/vulkan_context.h"
#include "engine/pipelines.h"
#include "engine/vulkan_util.h"
#include "shapes/shape.h"
#include "geometry3d/mesh3d.h"
#include "text/text_renderer.h"
#include "ui/ui_manager.h"
#include "ui/ui_widgets.h"
#include "ui/ui_builder.h"
#include "ui/ui_json.h"
#include "ecs/coordinator.h"
#include "ecs/systems.h"
#include "ecs/asset_manager.h"
#include "ecs/prefab.h"
#include "ecs/scene.h"
#include "ecs/profiler.h"
#include "ecs/input_state.h"
#include "ecs/time_state.h"

// v1.0.2 逻辑/事件模块
#include "engine/event_system.h"
#include "engine/timers.h"
#include "engine/tween.h"

// v1.0.1 渲染引擎扩展模块
#include "render/texture.h"
#include "render/framebuffer.h"
#include "render/postfx.h"
#include "render/skybox.h"
#include "render/shadow.h"
#include "render/instancing.h"
#include "render/particles.h"
#include "render/mesh_loader.h"
// v1.0.2 调试可视化
#include "render/debug_render.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

// 原演示场景（SolarSystemScene / FollowAndColliderScene / UiDemoScene）已删除：
// 项目现为纯引擎库，请在 registerScenes() 中注册你自己的 Scene 子类。





// ============================================================================
// 辅助：定位 config.json 路径（同前实现）
// ============================================================================
static std::string getConfigPath()
{
    {
        std::ifstream test("config.json");
        if (test.is_open()) { test.close(); return "config.json"; }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
            return p.substr(0, pos) + "\\config.json";
    }
#endif
    return "config.json";
}

// 资源目录：尝试 cwd/shaders，回退到 exe 同目录
static std::string getShaderDir()
{
    {
        std::ifstream test("shaders/basic.vert.spv");
        if (test.is_open()) { test.close(); return "shaders"; }
    }
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0)
    {
        std::string p(exePath);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos)
            return p.substr(0, pos) + "\\shaders";
    }
#endif
    return "shaders";
}

// ============================================================================
// 辅助：把 obj_loader::Mesh 转成渲染顶点（pos3 + normal3 + color3，stride 36）
// 供 Instancing::uploadMesh 使用（实例网格 + OBJ 加载模块演示）
// ============================================================================
static std::vector<float> meshToRenderVerts(const obj_loader::Mesh& m,
                                            float r, float g, float b)
{
    std::vector<float> out;
    out.reserve(m.vertices.size() * 9);
    for (const auto& v : m.vertices)
    {
        out.insert(out.end(), { v.position.x, v.position.y, v.position.z,
                                v.normal.x,   v.normal.y,   v.normal.z,
                                r, g, b });
    }
    return out;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

VulkanApp::VulkanApp()
{
    loadConfig();
    createWindow();
    createVulkanInstance();

    m_ctx = std::make_unique<VulkanContext>(m_instance, m_window,
        m_config.windowWidth, m_config.windowHeight);

    createEngineResources();

    // v1.0.1 渲染链：离屏场景 RT / 后处理 / 天空 / 阴影 / 实例化 / 粒子
    createRenderModules();

    // ECS 外部上下文（Vulkan 句柄，供 SpawnerSystem 上传粒子）
    m_input.windowWidth  = m_config.windowWidth;
    m_input.windowHeight = m_config.windowHeight;
    m_ecsCtx = std::make_unique<ecs::EcsVulkanContext>();
    m_ecsCtx->device           = m_ctx->device();
    m_ecsCtx->physicalDevice   = m_ctx->physicalDevice();
    m_ecsCtx->commandPool      = m_ctx->commandPool();
    m_ecsCtx->graphicsQueue    = m_ctx->graphicsQueue();
    m_ecsCtx->meshSetLayout    = m_pipe3D->descriptorSetLayout();
    m_ecsCtx->meshPool         = m_meshDescPool;
    m_ecsCtx->windowWidth      = m_config.windowWidth;
    m_ecsCtx->windowHeight     = m_config.windowHeight;

    // 资源工厂 + 预制件：在 createDemoScene 之前初始化好
    m_assets = std::make_unique<ecs::AssetManager>();
    initAssetManager();
    m_prefabs = std::make_unique<ecs::PrefabRegistry>();
    registerPrefabs();

    // 场景管理器 + 性能分析器 + 输入动作映射器
    m_scenes      = std::make_unique<ecs::SceneManager>();
    m_profiler    = std::make_unique<ecs::Profiler>();
    m_inputMapper = std::make_unique<ecs::InputMapper>();
    registerInputActions();  // 注册默认动作（forward/jump/...）
    m_inputMapper->setInputState(&m_input);

    // ECS 协调器初始化 + 注册默认系统 + 注入外部上下文
    m_ecs = std::make_unique<ecs::Coordinator>();
    registerEcsSystems();
    bindEcsExternalContexts();

    // 场景注册（引擎入口：在此注册你的自定义 Scene）
    registerScenes();

    createDemoScene();

    m_ui = std::make_unique<UiManager>();
    // UI 树为空，用户可通过 m_ui->setRoot() 或 m_ui->loadFromFile() 加载自定义 UI。

    setupCallbacks();
    m_resourcesReady = true;
    LOG_INFO("VulkanApp", "ctor", "图像引擎初始化完成");
}

VulkanApp::~VulkanApp()
{
    // 先等待 GPU 空闲
    if (m_ctx) vkDeviceWaitIdle(m_ctx->device());

    m_ui.reset();
    // v1.0.1 渲染链模块（独立描述符池，需在 m_ctx 之前释放）
    destroyRenderModules();
    // ECS 必须在 m_ctx 之前释放：组件中 unique_ptr<Mesh3D/Shape> 析构需用 m_device
    m_ecs.reset();
    // 场景管理器 / 性能分析器 / 输入映射器先于资源工厂清空（避免悬挂引用）
    m_scenes.reset();
    m_profiler.reset();
    m_inputMapper.reset();
    // 预制件/资源工厂仅持有弱引用，但提前清空让悬挂引用更早暴露
    m_prefabs.reset();
    m_assets.reset();
    m_titleText.reset();
    m_shapes.clear();
    m_meshes.clear();

    // 先清空延迟销毁队列：m_ecs.reset() 已把 Mesh3D 的描述符集/缓冲登记进队列，
    // 而描述符集来自下面的 m_meshDescPool。必须先释放它们再销毁池，
    // 否则 vkFreeDescriptorSets 会使用已销毁的池（未定义行为）。
    if (m_ctx) vulkan_util::flushAllDeferredDestroy(m_ctx->device());

    // 销毁 3D 网格共享描述符池（必须在 mesh 析构之后，描述符集从池分配）
    if (m_meshDescPool != VK_NULL_HANDLE && m_ctx)
    {
        vkDestroyDescriptorPool(m_ctx->device(), m_meshDescPool, nullptr);
        m_meshDescPool = VK_NULL_HANDLE;
    }

    m_pipeText.reset();
    m_pipe3D.reset();
    m_pipe2DLineStrip.reset();
    m_pipe2DLine.reset();
    m_pipe2DFan.reset();
    m_pipe2D.reset();
    m_ctx.reset();

    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    LOG_INFO("VulkanApp", "dtor", "已销毁");
}

// ============================================================================
// 注册 ECS 默认系统
// 顺序：输入→相机→运动→碰撞→灯光→层级→生命周期→生成器→调试→渲染
//       渲染系统 onUpdate 为空，实际渲染由 drawEcs*() 调用 RenderSystem::render。
//       v1.6 新增：LightingSystem（收集主光）、ColliderSystem（AABB 碰撞 + 事件）
//                  排在 MovementSystem 之后保证用本帧最新位置检测碰撞
// ============================================================================
void VulkanApp::registerEcsSystems()
{
    m_ecs->systems().registerSystem<ecs::InputSystem>();
    m_ecs->systems().registerSystem<ecs::CameraSystem>();
    m_ecs->systems().registerSystem<ecs::MovementSystem>();
    m_ecs->systems().registerSystem<ecs::ColliderSystem>();
    m_ecs->systems().registerSystem<ecs::TriggerSystem>();   // v1.0.2 区域触发器
    m_ecs->systems().registerSystem<ecs::LightingSystem>();
    m_ecs->systems().registerSystem<ecs::HierarchySystem>();
    m_ecs->systems().registerSystem<ecs::LifetimeSystem>();
    m_ecs->systems().registerSystem<ecs::SpawnerSystem>();
    m_ecs->systems().registerSystem<ecs::DebugSystem>();
    m_ecs->systems().registerSystem<ecs::RenderSystem3D>();
    m_ecs->systems().registerSystem<ecs::RenderSystem2D>();
    m_ecs->systems().registerSystem<ecs::RenderSystemText>();
    LOG_INFO("ECS", "registerEcsSystems", "已注册 %zu 个系统",
             m_ecs->systems().listNames().size());
}

// ============================================================================
// 注入外部上下文：把 m_input / m_ecsCtx / m_time 指针交给需要它们的系统
// 必须在 registerEcsSystems 之后调用
// ============================================================================
void VulkanApp::bindEcsExternalContexts()
{
    if (auto* s = m_ecs->systems().find<ecs::InputSystem>())
        s->setInputState(&m_input);
    if (auto* s = m_ecs->systems().find<ecs::CameraSystem>())
        s->setInputState(&m_input);
    if (auto* s = m_ecs->systems().find<ecs::SpawnerSystem>())
        s->setVulkanContext(m_ecsCtx.get());
    if (auto* s = m_ecs->systems().find<ecs::DebugSystem>())
    {
        s->setTimeState(&m_time);
        s->setVulkanContext(m_ecsCtx.get());
    }
    // 注入 Profiler：所有系统自动获得性能采样能力（DebugSystem 会把它上屏显示）
    if (m_profiler && m_ecs)
        m_ecs->systems().setProfiler(m_profiler.get());
}

// ============================================================================
// 初始化 AssetManager：把 Vulkan 句柄塞进去
// ============================================================================
void VulkanApp::initAssetManager()
{
    m_assets->init(m_ctx->device(),
                   m_ctx->physicalDevice(),
                   m_ctx->commandPool(),
                   m_ctx->graphicsQueue(),
                   m_pipe3D->descriptorSetLayout(),
                   m_meshDescPool);
}

// ============================================================================
// 注册常用预制件：用户可按名字实例化，避免每次手写一长串参数
// 预制件工厂内可用 m_assets（通过 PrefabCtx.userData 传入 AssetManager*）
// ============================================================================
void VulkanApp::registerPrefabs()
{
    // 预制件工厂约定：ctx.userData 必须是 AssetManager* 指针
    // 若用户不传 ctx 也能用：直接闭包捕获 m_assets.get()（VulkanApp 内调用）
    ecs::AssetManager* assets = m_assets.get();

    // bullet：小型 2D 圆形粒子（用于 SpawnerSystem 实例化等）
    // 注意：SpawnerSystem 内部已直接 make_unique<Circle>，这里提供
    //       对外使用的"通用圆形粒子"预制件。
    m_prefabs->add("particle", [assets](ecs::Coordinator& c, ecs::Entity e,
                                         const ecs::PrefabCtx& ctx) {
        (void)ctx;
        // 默认参数；调用方可后续修改组件
        auto circle = assets->createCircle(0.0f, 0.0f, 0.03f, 12, 1.0f, 0.55f, 0.1f);
        c.addComponent<ecs::Shape2DComponent>(e, ecs::Shape2DComponent(std::move(circle)));
        c.addComponent<ecs::Movement>(e, ecs::Movement{});
        c.addComponent<ecs::Lifetime>(e, ecs::Lifetime{ 2.0f });
    });

    // sun_cube：橙色立方体（无 Transform，由调用方添加）
    m_prefabs->add("sun_cube", [assets](ecs::Coordinator& c, ecs::Entity e,
                                          const ecs::PrefabCtx& ctx) {
        (void)ctx;
        auto cube = assets->createCube(1.2f, 1.0f, 0.75f, 0.15f);
        c.addComponent<ecs::Mesh3DComponent>(e, ecs::Mesh3DComponent(std::move(cube)));
    });

    // planet：彩色多面体
    m_prefabs->add("planet", [assets](ecs::Coordinator& c, ecs::Entity e,
                                        const ecs::PrefabCtx& ctx) {
        (void)ctx;
        auto ico = assets->createSphere(0.5f, 0.3f, 0.6f, 0.95f);
        c.addComponent<ecs::Mesh3DComponent>(e, ecs::Mesh3DComponent(std::move(ico)));
    });

    LOG_INFO("ECS", "registerPrefabs", "已注册 %zu 个预制件", m_prefabs->size());
}

// ============================================================================
// 注册 InputMapper 默认动作映射（v1.6 新增）
//
// 业务层用语义动作名（"forward"/"jump"）查询，与具体 GLFW 键码解耦；
// 用户可在运行时调用 m_inputMapper->rebind(name, newKey) 自定义按键
// ============================================================================
void VulkanApp::registerInputActions()
{
    // 移动
    m_inputMapper->bind("forward",  GLFW_KEY_W);
    m_inputMapper->bind("forward",  GLFW_KEY_UP);
    m_inputMapper->bind("backward", GLFW_KEY_S);
    m_inputMapper->bind("backward", GLFW_KEY_DOWN);
    m_inputMapper->bind("left",     GLFW_KEY_A);
    m_inputMapper->bind("left",     GLFW_KEY_LEFT);
    m_inputMapper->bind("right",    GLFW_KEY_D);
    m_inputMapper->bind("right",    GLFW_KEY_RIGHT);
    m_inputMapper->bind("up",       GLFW_KEY_Q);
    m_inputMapper->bind("down",     GLFW_KEY_E);
    // 升降 / 跳跃 / 蹲下
    m_inputMapper->bind("jump",     GLFW_KEY_SPACE);
    m_inputMapper->bind("crouch",   GLFW_KEY_LEFT_CONTROL);
    m_inputMapper->bind("sprint",   GLFW_KEY_LEFT_SHIFT);
    // 自转
    m_inputMapper->bind("rot_cw",   GLFW_KEY_R);
    m_inputMapper->bind("rot_ccw",  GLFW_KEY_F);
    // 退出
    m_inputMapper->bind("quit",     GLFW_KEY_ESCAPE);

    LOG_INFO("Input", "registerInputActions",
             "已注册 %zu 个动作", m_inputMapper->listActions().size());
}

// ============================================================================
// 注册场景（引擎入口）
//
// 项目为纯引擎库，未内置任何演示场景。请在此注册你自己的 Scene 子类：
//   1. 在 src/ecs/scene.h 的 Scene 基类上派生（实现 name()/onEnter()）；
//   2. 在此调用 m_scenes->add<YourScene>()；
//   3. 调用 m_scenes->switchTo(*m_ecs, "your_scene", userCtx) 切到初始场景。
// 场景内通过 Scene::ctxAs<T>() 取回注入的用户上下文（详见 ecs/scene.h）。
// ============================================================================
void VulkanApp::registerScenes()
{
    LOG_INFO("Scene", "registerScenes",
             "引擎就绪：请注册你的自定义场景（当前已注册 %zu 个）",
             m_scenes->size());
}

// ============================================================================
// 配置加载
// ============================================================================

void VulkanApp::loadConfig()
{
    std::string path = getConfigPath();
    std::ifstream f(path);
    if (!f.is_open())
    {
        LOG_WARN("Config", "loadConfig", "未找到 config.json 路径=%s，使用默认配置", path.c_str());
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    // 统一走 ui_json 的递归下降解析器：类型安全、支持 JSONC 注释/尾随逗号，
    // 与 ui_loader 复用同一解析器，避免脆弱正则逐字段匹配。
    JsonPtr root = parseJson(content);
    if (!root || !root->isObject())
    {
        LOG_WARN("Config", "loadConfig", "config.json 解析失败 path=%s，使用默认配置",
                 path.c_str());
        return;
    }

    if (const JsonValue* v = root->find("window_title"))
        m_config.windowTitle = v->asString(m_config.windowTitle);
    if (const JsonValue* v = root->find("window_width"))
    {
        double d = v->asNumber(-1.0);
        if (d > 0) m_config.windowWidth = static_cast<uint32_t>(d);
    }
    if (const JsonValue* v = root->find("window_height"))
    {
        double d = v->asNumber(-1.0);
        if (d > 0) m_config.windowHeight = static_cast<uint32_t>(d);
    }
    if (const JsonValue* v = root->find("aspect_mode"))
        m_config.aspectMode = v->asString(m_config.aspectMode);
    if (const JsonValue* v = root->find("msaa_samples"))
    {
        double d = v->asNumber(static_cast<double>(m_config.msaaSamples));
        if (d == 1.0 || d == 2.0 || d == 4.0 || d == 8.0)
            m_config.msaaSamples = static_cast<uint32_t>(d);
        else
            LOG_WARN("Config", "loadConfig", "非法 msaa_samples=%g，已回退到默认 %u",
                     d, m_config.msaaSamples);
    }

    // 校验 aspect_mode 合法值，非法值回退到 stretch 并警告
    if (m_config.aspectMode != "stretch" && m_config.aspectMode != "letterbox")
    {
        LOG_WARN("Config", "loadConfig", "未知 aspect_mode=\"%s\"，已回退到默认 \"stretch\"",
                 m_config.aspectMode.c_str());
        m_config.aspectMode = "stretch";
    }

    LOG_INFO("Config", "loadConfig", "已加载 path=%s  title=\"%s\"  size=%ux%u  mode=%s  msaa=%u",
             path.c_str(), m_config.windowTitle.c_str(),
             m_config.windowWidth, m_config.windowHeight,
             m_config.aspectMode.c_str(), m_config.msaaSamples);
}

// ============================================================================
// 主循环
// ============================================================================

void VulkanApp::run()
{
    LOG_INFO("VulkanApp", "run", "进入主循环（ESC 退出）");
    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();

    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }

        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        // dt 钳制：避免窗口拖动暂停后 dt 过大导致物体瞬移
        if (dt > 0.1f) dt = 0.1f;

        // 全局时间状态（FPS/帧数/elapsed，含时间缩放），供 DebugSystem 等系统读取
        m_time.tick(dt);

        // v1.0.2：定时器 / 缓动动画（受全局时间缩放影响）
        timers::system().update(m_time.dt);
        tween::system().update(m_time.dt);

        // 帧开始（Profiler）：与 frameEnd 配对，包住逻辑 + 渲染
        if (m_profiler) m_profiler->frameBegin();

        // 相机 aspect 实时跟随渲染区域（窗口 resize 自适应；首帧用交换链尺寸兜底）
        {
            VkExtent2D ext = (m_renderExtent.width && m_renderExtent.height)
                                 ? m_renderExtent : m_ctx->extent();
            m_input.windowWidth  = ext.width;
            m_input.windowHeight = ext.height;
        }

        // 场景级 onUpdate（场景自定义逻辑，与系统更新独立）
        if (m_scenes) m_scenes->update(dt);

        // ECS 逻辑系统更新（Input/Camera/Movement/Collider/Lighting/Hierarchy/
        //                       Lifetime/Spawner/Debug + 渲染 onUpdate 为空）
        if (m_ecs) m_ecs->updateSystems(dt);

        // 帧结束（Profiler）：统计系统级耗时（系统内部可用 PROFILE_SCOPE）
        if (m_profiler) m_profiler->frameEnd();

        // 鼠标 delta 已被 CameraSystem 消费，本帧末尾清零
        m_input.clearDeltas();
        // InputMapper 推进边沿状态：把 justPressed/justReleased 推到下一帧
        if (m_inputMapper) m_inputMapper->endFrame();

        renderFrame();
        if (m_ui) m_ui->update(dt);

        // 帧末：派发本帧排队事件（ECS 总线 + 引擎级全局总线）
        if (m_ecs) m_ecs->events().dispatch();
        events::system().dispatch();
    }

    // 退出前可选打印 Profiler 汇总
    if (m_profiler)
    {
        LOG_INFO("Profiler", "shutdown",
                 "近窗口平均帧=%.3f ms（约 %.1f FPS），窗口帧数=%llu",
                 m_profiler->getAvgFrameMs(),
                 m_profiler->getAvgFrameMs() > 0.0
                     ? 1000.0 / m_profiler->getAvgFrameMs() : 0.0,
                 static_cast<unsigned long long>(m_profiler->getSnapFrameCount()));
    }
}

// ============================================================================
// GLFW 窗口创建
// ============================================================================

void VulkanApp::createWindow()
{
    if (!glfwInit()) throw std::runtime_error("GLFW 初始化失败");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_config.windowWidth, m_config.windowHeight,
        m_config.windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); throw std::runtime_error("GLFW 窗口创建失败"); }
}

// ============================================================================
// Vulkan 实例
// ============================================================================

void VulkanApp::createVulkanInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = m_config.windowTitle.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Graphics Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount = 0;

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
    {
        LOG_ERROR("Vulkan", "createVulkanInstance", "vkCreateInstance 失败");
        throw std::runtime_error("vkCreateInstance 失败");
    }
    LOG_INFO("Vulkan", "createVulkanInstance", "Vulkan 实例创建成功");
}

void VulkanApp::setupDebugMessenger()
{
    // 此处不启用验证层，留作扩展点
}

// ============================================================================
// 引擎资源：管线
// ============================================================================

void VulkanApp::createEngineResources()
{
    std::string shaderDir = getShaderDir();
    VkExtent2D extent = m_ctx->extent();

    m_pipe2D     = std::make_unique<Pipeline2D>();
    m_pipe2D->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                     VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    m_pipe2DFan  = std::make_unique<Pipeline2D>();
    m_pipe2DFan->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN);

    m_pipe2DLine = std::make_unique<Pipeline2D>();
    m_pipe2DLine->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                         VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

    m_pipe2DLineStrip = std::make_unique<Pipeline2D>();
    m_pipe2DLineStrip->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir,
                              VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);

    m_pipe3D = std::make_unique<Pipeline3D>();
    m_pipe3D->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir);

    m_pipeText = std::make_unique<PipelineText>();
    m_pipeText->create(m_ctx->device(), m_ctx->renderPass(), extent, shaderDir);

    // 创建 3D 网格共享描述符池（供 Mesh3D::upload 使用）
    {
        VkDevice dev = m_ctx->device();
        // 容量：按场景中可能同时存在的 3D 模型/粒子峰值预留（默认 256，
        // 每个描述符集极小、成本可忽略）；生成器类系统（如 SpawnerSystem）
        // 持续创建/销毁 Mesh3D 时配合延迟销毁队列回收，不会耗尽。
        // v1.0.2：Mesh3D 描述符集含 UBO + 材质纹理（COMBINED_IMAGE_SAMPLER），
        // 池须为两类描述符预留容量，否则分配失败（VK_ERROR_OUT_OF_POOL_MEMORY）。
        VkDescriptorPoolSize dps[2]{};
        dps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        dps[0].descriptorCount = 256; // 预留：最多 256 个 3D 模型/粒子
        dps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        dps[1].descriptorCount = 256;
        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // 需要 FREE_DESCRIPTOR_SET_BIT：Mesh3D 粒子析构时会 vkFreeDescriptorSets
        // 回收描述符集，否则池在持续生成粒子时会耗尽（VK_ERROR_OUT_OF_POOL_MEMORY）
        dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpci.maxSets = 256;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = dps;
        if (vkCreateDescriptorPool(dev, &dpci, nullptr, &m_meshDescPool) != VK_SUCCESS)
            throw std::runtime_error("创建 3D 网格描述符池失败");
    }
}

// ============================================================================
// v1.0.1 渲染链模块：离屏场景 RT → 后处理 → 主 pass → 2D/UI 叠加
//
// 各模块均为独立 API（render/ 目录），此处负责装配与每帧调度：
//   1. 阴影深度 pass（ShadowMap 生成方向光深度贴图）
//   2. 场景渲染进离屏 RT（Skybox 背景 + Mesh3D + 实例化 + 粒子）
//   3. 后处理（PostFx）输出到主 pass
//   4. 2D/UI 叠加在主 pass（letterbox 视口保持一致）
// ============================================================================

void VulkanApp::createRenderModules()
{
    VkExtent2D ext = m_ctx->extent();
    std::string shaderDir = getShaderDir();

    // 渲染模块共享句柄（descriptorPool 置空：各模块自建描述符池，互不干扰）
    m_renderDev = m_ctx->renderDevice();
    m_renderDev.descriptorPool = VK_NULL_HANDLE;

    // MSAA：config 指定级别，并 clamp 到物理设备支持的最大颜色采样数
    VkSampleCountFlagBits msaa = VK_SAMPLE_COUNT_1_BIT;
    {
        VkSampleCountFlagBits count = VK_SAMPLE_COUNT_1_BIT;
        switch (m_config.msaaSamples)
        {
            case 2:  count = VK_SAMPLE_COUNT_2_BIT;  break;
            case 4:  count = VK_SAMPLE_COUNT_4_BIT;  break;
            case 8:  count = VK_SAMPLE_COUNT_8_BIT;  break;
            default: count = VK_SAMPLE_COUNT_1_BIT;  break;
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_ctx->physicalDevice(), &props);
        const VkSampleCountFlags supported = props.limits.framebufferColorSampleCounts;
        if ((count & supported) == 0)
        {
            // 请求级别不支持 → 逐级降档
            if ((VK_SAMPLE_COUNT_4_BIT & supported) != 0)      count = VK_SAMPLE_COUNT_4_BIT;
            else if ((VK_SAMPLE_COUNT_2_BIT & supported) != 0) count = VK_SAMPLE_COUNT_2_BIT;
            else                                                count = VK_SAMPLE_COUNT_1_BIT;
            LOG_WARN("VulkanApp", "createRenderModules",
                     "设备不支持 %ux MSAA，已降级到 %ux", m_config.msaaSamples,
                     static_cast<uint32_t>(count));
        }
        msaa = count;
    }

    try
    {
        // 离屏场景目标：颜色格式与主 pass 一致，保证 2D/3D 管线可复用；
        // samples>1 时启用 MSAA（3D 场景抗锯齿，渲染后自动 resolve 供后处理采样）
        m_sceneRT = RenderTarget::create(m_renderDev, ext.width, ext.height,
                                         /*withDepth=*/true,
                                         m_ctx->imageFormat(), m_ctx->depthFormat(),
                                         msaa);
        if (!m_sceneRT) throw std::runtime_error("RenderTarget 创建失败");

        // 渲染链专用 3D 管线：绑定离屏 RT renderPass，采样级别与 RT 一致
        m_pipe3DRT = std::make_unique<Pipeline3D>();
        m_pipe3DRT->create(m_ctx->device(), m_sceneRT->renderPass(), ext, shaderDir, msaa);

        // 后处理：输出到主 renderPass（全屏三角形，无顶点输入）
        m_postfx = std::make_unique<PostFx>();
        m_postfx->create(m_renderDev, m_ctx->renderPass(), shaderDir);

        // 天空盒 + 环境光：绘制在离屏 RT 的 renderPass 内（作为 3D 背景）
        m_skybox = std::make_unique<Skybox>();
        m_skybox->create(m_renderDev, m_sceneRT->renderPass(), shaderDir, msaa);
        m_skybox->setSunDirection(glm::normalize(glm::vec3(0.5f, 1.0f, -0.4f)));
        m_skybox->setSunIntensity(1.2f);

        // 阴影贴图：1024 深度图 + depth-only 管线
        m_shadow = std::make_unique<ShadowMap>();
        m_shadow->create(m_renderDev, shaderDir, 1024);

        // 实例化渲染：绘制在离屏 RT 的 renderPass 内
        m_instancing = std::make_unique<Instancing>();
        m_instancing->create(m_renderDev, m_sceneRT->renderPass(), shaderDir, msaa);

        // CPU 粒子系统：绘制在离屏 RT 的 renderPass 内
        m_particles = std::make_unique<ParticleSystem>();
        m_particles->create(m_renderDev, m_sceneRT->renderPass(), shaderDir, msaa);
        m_particles->setCapacity(m_renderDev, 2048);
        // 渐变光点纹理（程序化生成，无外部资源依赖）
        m_particleTex = Texture::createGradient(m_renderDev, 64, 64, true);
        if (m_particleTex && m_particleTex->valid())
            m_particles->setTexture(m_renderDev, m_particleTex.get());

        // v1.0.2 调试可视化（绘制在离屏 RT 的 renderPass 内）
        m_debug = std::make_unique<DebugRenderer>();
        m_debug->create(m_renderDev, m_sceneRT->renderPass(), shaderDir, msaa);

        m_renderChainEnabled = true;
        LOG_INFO("VulkanApp", "createRenderModules",
                 "渲染链就绪：阴影→场景RT(MSAA %ux)→后处理→主pass→UI",
                 static_cast<uint32_t>(msaa));
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("VulkanApp", "createRenderModules",
                  "渲染链初始化失败，回退传统直绘路径：%s", e.what());
        destroyRenderModules();
        m_renderChainEnabled = false;
    }
}

void VulkanApp::destroyRenderModules()
{
    // 注意：粒子系统先于粒子纹理释放（系统持有纹理指针，不拥有）
    m_particles.reset();
    m_particleTex.reset();
    m_debug.reset();
    m_instancing.reset();
    m_shadow.reset();
    m_skybox.reset();
    m_postfx.reset();
    m_pipe3DRT.reset();
    m_sceneRT.reset();
    m_renderChainEnabled = false;
}

// 阴影深度 pass：光源视角渲染全部 m_meshes 到深度贴图
void VulkanApp::renderShadowPass(VkCommandBuffer cmd)
{
    if (!m_shadow || !m_shadow->descriptorSet() || m_meshes.empty()) return;

    // 主光方向：优先 ECS LightingSystem，回退默认
    glm::vec3 lightDir{ 0.0f, -1.0f, 0.3f };
    if (auto* lsys = m_ecs ? m_ecs->systems().find<ecs::LightingSystem>() : nullptr)
        if (const ecs::Light* l = lsys->primaryLight()) lightDir = l->direction;

    glm::mat4 lightVP = ShadowMap::computeLightVP(lightDir,
                                                  glm::vec3(0.0f, 0.0f, 0.0f),
                                                  12.0f);
    m_shadow->setLightMatrix(cmd, lightVP);
    m_shadow->begin(cmd);
    for (auto& m : m_meshes)
        m->drawDepth(cmd, m_shadow->depthLayout(), m->model());
    m_shadow->end(cmd);
}

// 场景渲染进离屏 RT：天空盒背景 → 3D 网格 → 实例化 → 粒子
void VulkanApp::renderSceneToRT(VkCommandBuffer cmd, float dt)
{
    if (!m_sceneRT || !m_skybox) return;
    const uint32_t rtW = m_sceneRT->width();
    const uint32_t rtH = m_sceneRT->height();
    if (rtW == 0 || rtH == 0) return;

    // 当前目标视口 = RT 全尺寸（离屏图无 letterbox 偏移）
    m_activeViewport = { 0.0f, 0.0f, static_cast<float>(rtW),
                         static_cast<float>(rtH), 0.0f, 1.0f };
    m_activeScissor  = { {0, 0}, {rtW, rtH} };

    VkClearColorValue clear{ {0.05f, 0.05f, 0.08f, 1.0f} };
    m_sceneRT->begin(cmd, clear);
    vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);

    // 相机（ECS 主相机或默认）
    glm::mat4 view = currentCameraView();
    float aspect = static_cast<float>(rtW) / static_cast<float>(rtH ? rtH : 1);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;   // Vulkan Y 翻转

    // 1) 天空盒背景（关闭深度测试，作为底色）
    m_skybox->draw(cmd, view, aspect);

    // 2) 3D 网格（传统容器 + ECS），动画由 elapsed 驱动
    float t = m_time.elapsed;
    for (size_t i = 0; i < m_meshes.size(); ++i)
    {
        if (!m_meshes[i]) continue;
        // 第 0 个是地面（不旋转），其余悬浮体旋转
        if (i > 0)
        {
            float speed = 0.6f + 0.15f * static_cast<float>(i);
            glm::mat4 m = glm::rotate(glm::mat4(1.0f), t * speed,
                                      glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::rotate(m, t * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));
            m_meshes[i]->setModel(m);
        }
    }
    draw3DMeshes();
    if (m_ecs) drawEcs3D();

    // 3) 实例化渲染（旋转立方体云：每帧更新实例矩阵/颜色）
    if (m_instancing)
    {
        m_instancing->setLight(glm::normalize(glm::vec3(-0.5f, -1.0f, 0.4f)),
                               glm::vec3(1.0f), 0.35f);
        const size_t kN = 120;
        std::vector<glm::mat4> mats(kN);
        std::vector<glm::vec4> colors(kN);
        for (size_t k = 0; k < kN; ++k)
        {
            float ang = (static_cast<float>(k) / static_cast<float>(kN)) * 6.28318f + t * 0.4f;
            float rad = 2.2f + 0.6f * static_cast<float>(k % 3);
            float y   = -0.9f + 0.9f * static_cast<float>((k / 3) % 3);
            glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                         glm::vec3(std::cos(ang) * rad, y, std::sin(ang) * rad));
            float s = 0.18f + 0.05f * static_cast<float>(k % 5);
            m = glm::scale(m, glm::vec3(s));
            m = glm::rotate(m, t * 1.2f, glm::vec3(0.3f, 1.0f, 0.0f));
            mats[k] = m;
            float hue = static_cast<float>(k) / static_cast<float>(kN);
            colors[k] = glm::vec4(0.5f + 0.5f * std::sin(hue * 6.28318f),
                                  0.5f + 0.5f * std::sin(hue * 6.28318f + 2.1f),
                                  0.5f + 0.5f * std::sin(hue * 6.28318f + 4.2f), 1.0f);
        }
        m_instancing->setInstances(m_renderDev, mats.data(), kN, colors.data());
        m_instancing->draw(cmd, view, proj, kN);
    }

    // 4) 粒子系统（每帧补充少量 + 推进模拟 + 绘制）
    if (m_particles)
    {
        if (m_particles->alive() < 1200)
        {
            m_particles->spawn(m_renderDev,
                               glm::vec3(0.0f, 0.4f, 0.0f),
                               glm::vec3(0.0f, 1.2f, 0.0f), 1.5f, 4, 2.5f, 0.05f,
                               glm::vec4(1.0f, 0.85f, 0.5f, 1.0f),
                               glm::vec4(0.2f, 0.3f, 0.9f, 0.0f), -0.8f);
        }
        m_particles->update(dt);
        m_particles->draw(cmd, view, proj, static_cast<float>(rtH));
    }

    // 5) 调试可视化（v1.0.2）：每帧 clear 后收集图元，一次批量绘制。
    //    用户可通过 debugRenderer() 在场景逻辑中收集自己的调试图元。
    if (m_debug)
    {
        m_debug->clear();
        m_debug->grid(5.0f, 1.0f, glm::vec3(0.22f, 0.22f, 0.28f));
        m_debug->axes(glm::vec3(0.0f, 0.01f, 0.0f), 1.2f);
        m_debug->box(glm::vec3(0.0f, -0.02f, 0.0f), glm::vec3(3.6f, 0.05f, 3.6f),
                     glm::vec3(0.6f, 0.5f, 0.42f));
        m_debug->render(cmd, view, proj);
    }

    m_sceneRT->end(cmd);
}

// 重新开始主 render pass（渲染链路径：beginFrame 已开始的主 pass 被提前结束）
void VulkanApp::beginMainPass(VkCommandBuffer cmd, uint32_t imageIndex)
{
    VkRenderPassBeginInfo rbi{};
    rbi.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass  = m_ctx->renderPass();
    rbi.framebuffer = m_ctx->framebuffer(imageIndex);
    rbi.renderArea  = VkRect2D{ {0, 0}, m_ctx->extent() };
    VkClearValue cvs[2];
    cvs[0].color        = { {0.05f, 0.05f, 0.08f, 1.0f} };
    cvs[1].depthStencil = { 1.0f, 0 };
    rbi.clearValueCount = 2;
    rbi.pClearValues    = cvs;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
}

glm::mat4 VulkanApp::currentCameraView() const
{
    if (m_ecs)
    {
        const ecs::Camera* cam = ecs::CameraSystem::findPrimary(*m_ecs);
        if (cam) return cam->viewMatrix;
    }
    return glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                       glm::vec3(0.0f, 0.0f, 0.0f),
                       glm::vec3(0.0f, 1.0f, 0.0f));
}

// ============================================================================
// 用户自定义场景入口
//
// 推荐在 registerScenes() 中注册 Scene 子类（场景内容写在 onEnter()）。
// 本函数保留为空存根：适用于不引入 Scene 子类的轻量测试——直接在此
// m_ecs->entity(...) 创建实体即可（每帧经 m_ecs->updateSystems 驱动）。
// ============================================================================
void VulkanApp::createDemoScene()
{
    // 引擎初始化完成后的轻量扩展点；默认不创建任何内容。

    // ===== v1.0.1 渲染链演示内容（可自由增删） =====
    if (!m_ctx || !m_pipe3D) return;
    VkDevice dev = m_ctx->device();

    // 传统 3D 网格：地面（不旋转）+ 悬浮体（renderSceneToRT 中驱动旋转）
    {
        auto floor = std::make_unique<Cube>(9.0f, 0.55f, 0.50f, 0.45f);
        floor->setModel(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.3f, 0.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.05f, 1.0f)));
        floor->upload(dev, m_ctx->physicalDevice(), m_ctx->commandPool(),
                      m_ctx->graphicsQueue(), m_pipe3D->descriptorSetLayout(),
                      m_meshDescPool);
        m_meshes.push_back(std::move(floor));

        for (int i = 1; i <= 4; ++i)
        {
            std::unique_ptr<Mesh3D> mesh;
            if (i % 2)
                mesh = std::make_unique<Polyhedron>(Polyhedron::Type::Icosahedron, 0.55f);
            else
                mesh = std::make_unique<Cube>(0.8f);
            mesh->setModel(glm::translate(glm::mat4(1.0f),
                                          glm::vec3(0.0f, -0.15f, 0.0f)));
            mesh->upload(dev, m_ctx->physicalDevice(), m_ctx->commandPool(),
                         m_ctx->graphicsQueue(), m_pipe3D->descriptorSetLayout(),
                         m_meshDescPool);
            m_meshes.push_back(std::move(mesh));
        }
    }

    // 实例化渲染基准网格：内嵌 OBJ 立方体文本（验证 mesh_loader 模块，无需磁盘文件）
    if (m_instancing)
    {
        const char* cubeObj =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 1 1 0\n"
            "v 0 1 0\n"
            "v 0 0 1\n"
            "v 1 0 1\n"
            "v 1 1 1\n"
            "v 0 1 1\n"
            "f 1 2 3 4\n"
            "f 5 6 7 8\n"
            "f 1 2 6 5\n"
            "f 2 3 7 6\n"
            "f 3 4 8 7\n"
            "f 4 1 5 8\n";
        obj_loader::Mesh om;
        if (obj_loader::loadFromText(cubeObj, om))
        {
            std::vector<float> verts = meshToRenderVerts(om, 0.9f, 0.9f, 0.95f);
            m_instancing->uploadMesh(m_renderDev, verts, om.indices);
            // 建立实例缓冲容量（1 个占位，renderSceneToRT 每帧更新）
            glm::mat4 init = glm::mat4(1.0f);
            m_instancing->setInstances(m_renderDev, &init, 1);
            LOG_INFO("Demo", "createDemoScene",
                     "实例化基准网格就绪（OBJ 解析 %zu 顶点 / %zu 索引）",
                     om.vertices.size(), om.indices.size());
        }
    }
}

// ============================================================================
// 帧渲染
// ============================================================================

void VulkanApp::renderFrame()
{
    uint32_t imageIndex = m_ctx->beginFrame();
    if (imageIndex == ~0u)
    {
        // 交换链重建，跳过本帧
        return;
    }
    VkCommandBuffer cmd = m_ctx->commandBuffer();

    // 3D 管线选择：渲染链路径使用绑定 RT renderPass 的管线（采样级别与 RT 一致）
    m_pipe3DActive = (m_renderChainEnabled && m_pipe3DRT) ? m_pipe3DRT.get() : m_pipe3D.get();

    computeRenderArea();

    if (m_renderChainEnabled && m_sceneRT && m_postfx)
    {
        // 离屏 RT 尺寸跟随渲染区域（窗口 resize / letterbox 变化）
        if (m_sceneRT->width()  != m_renderExtent.width ||
            m_sceneRT->height() != m_renderExtent.height)
        {
            if (m_renderExtent.width == 0 || m_renderExtent.height == 0)
            {
                m_renderExtent = m_ctx->extent();
            }
            m_sceneRT->resize(m_renderDev, m_renderExtent.width, m_renderExtent.height);
        }

        // beginFrame 已开始主 pass —— 提前结束，执行离屏渲染链
        vkCmdEndRenderPass(cmd);

        // 1) 阴影深度 pass
        renderShadowPass(cmd);

        // 2) 场景渲染进离屏 RT
        renderSceneToRT(cmd, m_time.dt);

        // 3) 重新开始主 pass：后处理输出 + 2D/UI 叠加
        beginMainPass(cmd, imageIndex);
        m_activeViewport = m_renderViewport;
        m_activeScissor  = m_renderScissor;
        vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
        vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);
        m_postfx->apply(*m_sceneRT, cmd);

        draw2DShapes();
        drawText();
        if (m_ecs)
        {
            drawEcs2D();
            drawEcsText();
        }
        drawUI(cmd);
    }
    else
    {
        // 传统直绘路径（渲染链不可用时回退）
        m_activeViewport = m_renderViewport;
        m_activeScissor  = m_renderScissor;

        draw2DShapes();
        draw3DMeshes();
        drawText();

        // ECS 渲染路径（与上面并存，由 ECS 内 Shape2DComponent/Mesh3DComponent/TextComponent 驱动）
        if (m_ecs)
        {
            drawEcs2D();
            drawEcs3D();
            drawEcsText();
        }
        drawUI(cmd);
    }

    m_ctx->endFrame();
}

// ============================================================================
// ECS 渲染路径 —— 调用对应 RenderSystem 遍历实体
// ============================================================================
void VulkanApp::drawEcs2D()
{
    if (!m_ecs) return;
    auto* sys = m_ecs->systems().find<ecs::RenderSystem2D>();
    if (!sys) return;
    ecs::RenderSystem2D::Pipelines pipes{
        m_pipe2D->pipeline(),
        m_pipe2DFan->pipeline(),
        m_pipe2DLine->pipeline(),
        m_pipe2DLineStrip->pipeline(),
        m_pipe2D->pipelineLayout()
    };
    sys->render(*m_ecs, m_ctx->commandBuffer(), pipes, m_activeViewport, m_activeScissor);
}

void VulkanApp::drawEcs3D()
{
    if (!m_ecs) return;
    auto* sys = m_ecs->systems().find<ecs::RenderSystem3D>();
    if (!sys) return;

    // 优先使用 ECS 主相机；若场景无相机实体，回退到默认相机
    glm::mat4 view, proj;
    const ecs::Camera* cam = ecs::CameraSystem::findPrimary(*m_ecs);
    if (cam)
    {
        view = cam->viewMatrix;
        proj  = cam->projMatrix;
    }
    else
    {
        view = glm::lookAt(glm::vec3(0, 0, 5.0f),
                           glm::vec3(0, 0, 0),
                           glm::vec3(0, 1, 0));
        float aspect = static_cast<float>(m_renderExtent.width) /
                       static_cast<float>(m_renderExtent.height ? m_renderExtent.height : 1);
        proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        proj[1][1] *= -1.0f;  // Vulkan 翻转 Y
    }

    VkCommandBuffer cmd = m_ctx->commandBuffer();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe3DActive->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);

    // 注入本帧光照：LightingSystem 已扫描并缓存主方向光
    const ecs::Light* light = nullptr;
    if (auto* lsys = m_ecs->systems().find<ecs::LightingSystem>())
        light = lsys->primaryLight();

    glm::vec3 lightDir{ 0.0f, -1.0f, 0.3f };
    glm::vec3 lightColor{ 1.0f, 1.0f, 1.0f };
    float     ambient  = 0.18f;
    if (light)
    {
        lightDir   = light->direction;
        lightColor = light->color * light->intensity;
    }

    sys->render(*m_ecs, cmd, m_pipe3DActive->pipelineLayout(), view, proj,
                &lightDir, &lightColor, ambient);
}

void VulkanApp::drawEcsText()
{
    if (!m_ecs) return;
    auto* sys = m_ecs->systems().find<ecs::RenderSystemText>();
    if (!sys) return;
    sys->render(*m_ecs, m_ctx->commandBuffer(),
                m_pipe2D->pipeline(), m_pipe2D->pipelineLayout(),
                m_activeViewport, m_activeScissor);
}

void VulkanApp::computeRenderArea()
{
    VkExtent2D ext = m_ctx->extent();

    if (m_config.aspectMode == "letterbox")
    {
        // 保持 config 中初始宽高比，不足部分用清除色填充
        float targetAspect = static_cast<float>(m_config.windowWidth) /
                             static_cast<float>(m_config.windowHeight ? m_config.windowHeight : 1);
        float winAspect    = static_cast<float>(ext.width) /
                             static_cast<float>(ext.height ? ext.height : 1);

        if (winAspect > targetAspect)
        {
            // 窗口偏宽 → 左右留白
            m_renderExtent.width  = static_cast<uint32_t>(static_cast<float>(ext.height) * targetAspect);
            m_renderExtent.height = ext.height;
        }
        else
        {
            // 窗口偏高 → 上下留白
            m_renderExtent.width  = ext.width;
            m_renderExtent.height = static_cast<uint32_t>(static_cast<float>(ext.width) / targetAspect);
        }
        // 确保渲染区域至少 1x1，否则零尺寸视口会导致 GPU 崩溃
        m_renderExtent.width  = std::max(1u, std::min(m_renderExtent.width, ext.width));
        m_renderExtent.height = std::max(1u, std::min(m_renderExtent.height, ext.height));

        m_renderViewport.x = (static_cast<float>(ext.width)  - static_cast<float>(m_renderExtent.width )) * 0.5f;
        m_renderViewport.y = (static_cast<float>(ext.height) - static_cast<float>(m_renderExtent.height)) * 0.5f;
    }
    else
    {
        // stretch：视口 = 全屏
        m_renderExtent = ext;
        m_renderViewport.x = 0.0f;
        m_renderViewport.y = 0.0f;
    }
    m_renderViewport.width    = static_cast<float>(m_renderExtent.width);
    m_renderViewport.height   = static_cast<float>(m_renderExtent.height);
    m_renderViewport.minDepth = 0.0f;
    m_renderViewport.maxDepth = 1.0f;

    m_renderScissor.offset = { static_cast<int32_t>(m_renderViewport.x),
                                static_cast<int32_t>(m_renderViewport.y) };
    m_renderScissor.extent  = m_renderExtent;
}

void VulkanApp::draw2DShapes()
{
    if (m_shapes.empty()) return;  // 性能优化：无图元直接返回，避免 4 次无意义 pipeline bind

    // 按拓扑分组绘制，每组只 BindPipeline/Viewport/Scissor 一次
    // 显著减少命令缓冲中的冗余命令（尤其 shape 数量多时）
    VkCommandBuffer cmd = m_ctx->commandBuffer();
    VkPipelineLayout layout = m_pipe2D->pipelineLayout();

    enum Group { kFilled, kFan, kLines, kLineStrip };
    auto drawGroup = [&](Group g) {
        // 性能优化：先扫描该组是否有图元，无则跳过 pipeline bind
        bool hasMatch = false;
        for (auto& s : m_shapes)
        {
            switch (g) {
            case kFilled:    hasMatch = !s->isLineStripTopology() && !s->isLineTopology() && !s->isFanTopology(); break;
            case kFan:       hasMatch =  s->isFanTopology(); break;
            case kLines:     hasMatch =  s->isLineTopology(); break;
            case kLineStrip: hasMatch =  s->isLineStripTopology(); break;
            }
            if (hasMatch) break;
        }
        if (!hasMatch) return;  // 空组：跳过该组

        VkPipeline pipe = VK_NULL_HANDLE;
        switch (g) {
        case kFilled:    pipe = m_pipe2D->pipeline();         break;
        case kFan:       pipe = m_pipe2DFan->pipeline();      break;
        case kLines:     pipe = m_pipe2DLine->pipeline();     break;
        case kLineStrip: pipe = m_pipe2DLineStrip->pipeline();break;
        }
        // 每组仅设置一次公共状态
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
        vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);

        for (auto& s : m_shapes)
        {
            bool match = false;
            switch (g) {
            case kFilled:    match = !s->isLineStripTopology() && !s->isLineTopology() && !s->isFanTopology(); break;
            case kFan:       match =  s->isFanTopology(); break;
            case kLines:     match =  s->isLineTopology(); break;
            case kLineStrip: match =  s->isLineStripTopology(); break;
            }
            if (match)
                s->drawVBOOnly(cmd, layout);
        }
    };

    drawGroup(kFilled);
    drawGroup(kFan);
    drawGroup(kLines);
    drawGroup(kLineStrip);
}

void VulkanApp::draw3DMeshes()
{
    // 无网格直接返回
    if (m_meshes.empty()) return;

    VkCommandBuffer cmd = m_ctx->commandBuffer();

    // 先设置一次公共状态：Pipeline / Viewport / Scissor
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe3DActive->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);

    // 默认 view/proj：ECS 主相机或回退 (0,0,3) 看向原点（与天空盒/渲染链一致）
    glm::mat4 view = currentCameraView();
    float aspect = static_cast<float>(m_renderExtent.width) /
                   static_cast<float>(m_renderExtent.height ? m_renderExtent.height : 1);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    // 翻转 Y（GLM 默认 OpenGL 坐标，Vulkan 需要 Y 翻转）
    proj[1][1] *= -1.0f;

    VkPipelineLayout layout = m_pipe3DActive->pipelineLayout();
    for (auto& m : m_meshes)
    {
        // 注意：若需要模型矩阵每帧变化（例如旋转、移动），在 draw3DMeshes 开头
        //       或 createDemoScene 外的自定义 update() 中 setModel()。
        // Mesh3D::drawVBOOnly 内部会自动检测 view/proj 变化并强制 UBO 刷新，
        // 因此相机移动或窗口缩放后模型会正确使用新矩阵渲染。
        m->drawVBOOnly(cmd, layout, view, proj);
    }
}

void VulkanApp::drawText()
{
    VkCommandBuffer cmd = m_ctx->commandBuffer();
    if (!m_titleText) return;

    // TextRenderer 继承自 Shape，顶点布局为 vec2 pos + vec3 color（5 float/顶点）。
    // 必须使用 Pipeline2D（basic.vert/frag）而非 PipelineText：
    // PipelineText 期望 vec2 pos + vec2 tex（4 float/顶点）且需要 sampler 描述符集，
    // 与 TextRenderer 的顶点布局不匹配，会导致 Validation Layer 报错和渲染乱码。
    // PipelineText 仅用于"字形图集纹理采样"模式（BitmapFont 图集上传到 GPU，
    // 通过 allocateDescriptorSet 分配 sampler），当前 TextRenderer 是"每像素一个小矩形"
    // 的纯 Shape 实现，不需要纹理。
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe2D->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);
    // 使用 drawVBOOnly：pipeline/viewport/scissor 已设置，仅需绑 VB + PushConst + Draw
    m_titleText->drawVBOOnly(cmd, m_pipe2D->pipelineLayout());
}

void VulkanApp::drawUI(VkCommandBuffer cmd)
{
    if (!m_ui || !m_ui->root()) return;
    // 性能优化：所有 UI 元素都用 Pipeline2D（pipelineFilled），
    // 在调用 m_ui->draw 之前预先绑定一次 pipeline/viewport/scissor，
    // 后续 UI 元素的 Shape 子对象只需 drawVBOOnly（仅绑 VB + PushConst + Draw），
    // 避免每个元素都重复 bind 同一个 pipeline。
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipe2D->pipeline());
    vkCmdSetViewport(cmd, 0, 1, &m_activeViewport);
    vkCmdSetScissor(cmd, 0, 1, &m_activeScissor);

    UiRenderContext ctx;
    ctx.device          = m_ctx->device();
    ctx.physicalDevice  = m_ctx->physicalDevice();
    ctx.commandPool     = m_ctx->commandPool();
    ctx.queue           = m_ctx->graphicsQueue();
    ctx.commandBuffer   = cmd;
    ctx.pipelineFilled  = m_pipe2D->pipeline();
    ctx.pipelineLine    = m_pipe2DLine->pipeline();
    ctx.pipelineLayout  = m_pipe2D->pipelineLayout();
    ctx.extent          = m_renderExtent;
    ctx.viewport        = m_activeViewport;
    ctx.scissor         = m_activeScissor;
    m_ui->draw(ctx);
}

// ============================================================================
// GLFW 回调
// ============================================================================

void VulkanApp::onFramebufferResize(GLFWwindow* w, int width, int height)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ctx) app->m_ctx->setResized();
}

void VulkanApp::onMouseMove(GLFWwindow* w, double x, double y)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;

    // 累积鼠标 delta（供 CameraSystem 自由飞行转头）
    app->m_input.mouseDeltaX += x - app->m_lastMouseX;
    app->m_input.mouseDeltaY += y - app->m_lastMouseY;
    app->m_lastMouseX = x;
    app->m_lastMouseY = y;
    app->m_input.mouseX = x;
    app->m_input.mouseY = y;

    if (!app->m_ui) return;
    // 将窗口鼠标坐标转换为渲染区域坐标
    // letterbox 模式下渲染区域在窗口中居中，存在偏移
    double rx = x - static_cast<double>(app->m_renderViewport.x);
    double ry = y - static_cast<double>(app->m_renderViewport.y);
    app->m_ui->onMouseMove(rx, ry);
}

void VulkanApp::onMouseButton(GLFWwindow* w, int b, int a, int /*m*/)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (!app) return;
    bool pressed = (a == GLFW_PRESS || a == GLFW_REPEAT);
    if      (b == GLFW_MOUSE_BUTTON_LEFT)   app->m_input.mouseLeft   = pressed;
    else if (b == GLFW_MOUSE_BUTTON_RIGHT)  app->m_input.mouseRight  = pressed;
    else if (b == GLFW_MOUSE_BUTTON_MIDDLE) app->m_input.mouseMiddle = pressed;
    if (app->m_ui) app->m_ui->onMouseButton(b, a, 0);
}

void VulkanApp::onKey(GLFWwindow* w, int k, int s, int a, int m)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app)
    {
        // 维护键盘状态数组（按下/重复都置 true，释放置 false）
        bool pressed = (a == GLFW_PRESS || a == GLFW_REPEAT);
        app->m_input.keys[static_cast<size_t>(k)] = pressed;
        if (app->m_ui) app->m_ui->onKey(k, s, a, m);
    }
}

void VulkanApp::onChar(GLFWwindow* w, unsigned int c)
{
    VulkanApp* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(w));
    if (app && app->m_ui) app->m_ui->onChar(c);
}

void VulkanApp::setupCallbacks()
{
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
    glfwSetCursorPosCallback(m_window, onMouseMove);
    glfwSetMouseButtonCallback(m_window, onMouseButton);
    glfwSetKeyCallback(m_window, onKey);
    glfwSetCharCallback(m_window, onChar);
}
