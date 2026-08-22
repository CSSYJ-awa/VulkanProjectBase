/**
 * systems.h —— 具体系统实现
 *
 * 系统 = 对一组具备特定组件的实体执行相同操作的逻辑。
 *
 * 逻辑系统：每帧 onUpdate 调用，仅修改组件数据
 *   - MovementSystem    ：Transform + Movement → 更新 position/rotationEuler
 *   - LifetimeSystem    ：Lifetime → 倒计时；<=0 销毁实体
 *
 * 渲染系统：暴露 render() 入口，由 VulkanApp::drawXxx 调用
 *   - RenderSystem3D    ：Transform + Mesh3DComponent → setModel + drawVBOOnly
 *   - RenderSystem2D    ：Shape2DComponent → drawVBOOnly（按拓扑分组）
 *   - RenderSystemText  ：TextComponent → drawVBOOnly
 *
 * 渲染系统依赖外部传入 VulkanContext/Pipeline 等（System 不持有 Vulkan 资源），
 * 由 VulkanApp::renderFrame() 调用 render(ctx, ...) 时按需传入。
 */
#pragma once

#include "system.h"
#include "coordinator.h"
#include "components.h"
#include "input_state.h"
#include "time_state.h"
#include "rng.h"
#include "profiler.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <cmath>
#include <algorithm>

class Pipeline3D;
class Pipeline2D;

namespace ecs {

// ============================================================================
// MovementSystem —— 逻辑系统：根据 Movement 更新 Transform
// ============================================================================
class MovementSystem : public System
{
public:
    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        auto view = coord.view<Transform, Movement>();
        for (auto e : view)
        {
            auto& tf = view.get<Transform>(e);
            auto& mv = view.get<Movement>(e);
            // 半隐式欧拉：先按加速度更新速度，再用新速度更新位置（更稳定）
            mv.velocity      += mv.acceleration     * dt;
            tf.position      += mv.velocity         * dt;
            tf.rotationEuler += mv.angularVelocity  * dt;
        }
    }
    const char* name() const override { return "MovementSystem"; }
};

// ============================================================================
// LifetimeSystem —— 逻辑系统：倒计时到期销毁实体
// ============================================================================
class LifetimeSystem : public System
{
public:
    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        auto view = coord.view<Lifetime>();
        std::vector<Entity> dead;
        dead.reserve(8);
        for (auto e : view)
        {
            auto& lt = view.get<Lifetime>(e);
            if (lt.remaining >= 0.0f)
            {
                lt.remaining -= dt;
                if (lt.remaining <= 0.0f) dead.push_back(e);
            }
        }
        for (auto e : dead) coord.destroyEntity(e);
    }
    const char* name() const override { return "LifetimeSystem"; }
};

// ============================================================================
// RenderSystem3D —— 渲染系统：渲染所有 Transform + Mesh3DComponent
//
// 用法（VulkanApp::draw3DMeshes 中）：
//   glm::mat4 view = ...; glm::mat4 proj = ...;
//   m_ecs->systems().find<RenderSystem3D>()->render(*m_ecs, cmd,
//                                                    m_pipe3D->pipelineLayout(),
//                                                    view, proj);
// ============================================================================
class RenderSystem3D : public System
{
public:
    // 渲染：必须在 pipeline/viewport/scissor 已绑定后调用。
    // lightDir/lightColor 为空时使用 Mesh3D 默认光照（保持向后兼容）。
    // v1.0.2 批处理优化：收集网格后按材质纹理分组排序（同纹理连续绘制），
    // 减少描述符集绑定切换；Mesh3D::setLight 内部对相同值有脏标志缓存。
    void render(Coordinator& coord,
                VkCommandBuffer cmd,
                VkPipelineLayout layout,
                const glm::mat4& view,
                const glm::mat4& proj,
                const glm::vec3* lightDir   = nullptr,
                const glm::vec3* lightColor = nullptr,
                float ambient = 0.15f)
    {
        auto v = coord.view<Transform, Mesh3DComponent>();
        // 收集阶段：写回模型矩阵 + 光照，同时记录纹理指针用于分组
        struct Item
        {
            Mesh3D*    mesh;
            const void* tex;
        };
        std::vector<Item> items;
        items.reserve(64);
        for (auto e : v)
        {
            auto& tf = v.get<Transform>(e);
            auto& mc = v.get<Mesh3DComponent>(e);
            if (!mc.mesh) continue;
            // 每帧把 Transform 模型矩阵写回 Mesh3D（drawVBOOnly 检测变化自动刷 UBO）
            mc.mesh->setModel(tf.modelMatrix());
            // 注入本帧光照（LightingSystem 收集的主光）
            if (lightDir)
            {
                glm::vec3 col = lightColor ? *lightColor : glm::vec3(1.0f);
                mc.mesh->setLight(*lightDir, col, ambient);
            }
            items.push_back({ mc.mesh.get(), mc.mesh->texture() });
        }
        if (items.empty()) return;

        // 按纹理指针排序（nullptr 无材质排最前）：同纹理网格连续绘制
        std::stable_sort(items.begin(), items.end(),
                         [](const Item& a, const Item& b) { return a.tex < b.tex; });
        for (const Item& it : items)
            it.mesh->drawVBOOnly(cmd, layout, view, proj);
    }
    const char* name() const override { return "RenderSystem3D"; }
};

// ============================================================================
// RenderSystem2D —— 渲染系统：渲染所有 Shape2DComponent
//
// 按 4 种拓扑分组绘制（与 VulkanApp::draw2DShapes 策略一致，复用同套 pipeline）：
//   kFilled / kFan / kLines / kLineStrip
// 每组只 bind pipeline/viewport/scissor 一次。
// ============================================================================
class RenderSystem2D : public System
{
public:
    enum Group { kFilled, kFan, kLines, kLineStrip, kGroupCount };

    // 渲染：传入所有 4 种 pipeline 句柄 + viewport/scissor
    // 调用者保证这些句柄有效（VulkanApp::createEngineResources 创建）
    struct Pipelines
    {
        VkPipeline filled     = VK_NULL_HANDLE;
        VkPipeline fan        = VK_NULL_HANDLE;
        VkPipeline lines      = VK_NULL_HANDLE;
        VkPipeline lineStrip  = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    void render(Coordinator& coord,
                VkCommandBuffer cmd,
                const Pipelines& pipes,
                const VkViewport& viewport,
                const VkRect2D&   scissor)
    {
        // 收集每个组的实体
        std::vector<Entity> groups[kGroupCount];
        for (int g = 0; g < kGroupCount; ++g) groups[g].reserve(8);

        auto v = coord.view<Shape2DComponent>();
        for (auto e : v)
        {
            auto& sc = v.get<Shape2DComponent>(e);
            if (!sc.shape) continue;
            if      (sc.shape->isLineTopology())     groups[kLines].push_back(e);
            else if (sc.shape->isLineStripTopology()) groups[kLineStrip].push_back(e);
            else if (sc.shape->isFanTopology())      groups[kFan].push_back(e);
            else                                      groups[kFilled].push_back(e);
        }

        VkPipeline pipesArr[kGroupCount] = { pipes.filled, pipes.fan, pipes.lines, pipes.lineStrip };

        for (int g = 0; g < kGroupCount; ++g)
        {
            if (groups[g].empty()) continue;  // 空组跳过
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipesArr[g]);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            for (auto e : groups[g])
            {
                auto& sc = coord.getComponent<Shape2DComponent>(e);
                sc.shape->drawVBOOnly(cmd, pipes.layout);
            }
        }
    }
    const char* name() const override { return "RenderSystem2D"; }
};

// ============================================================================
// RenderSystemText —— 渲染系统：渲染所有 TextComponent
//
// 与 VulkanApp::drawText 策略一致：单次 bind pipeline2D + viewport/scissor，
// 然后调每个 TextRenderer 的 drawVBOOnly。
// ============================================================================
class RenderSystemText : public System
{
public:
    void render(Coordinator& coord,
                VkCommandBuffer cmd,
                VkPipeline pipeline,
                VkPipelineLayout layout,
                const VkViewport& viewport,
                const VkRect2D&   scissor)
    {
        bool any = false;
        auto v = coord.view<TextComponent>();
        for (auto e : v)
        {
            auto& tc = v.get<TextComponent>(e);
            if (tc.renderer) { any = true; break; }
        }
        if (!any) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        for (auto e : v)
        {
            auto& tc = v.get<TextComponent>(e);
            if (tc.renderer)
                tc.renderer->drawVBOOnly(cmd, layout);
        }
    }
    const char* name() const override { return "RenderSystemText"; }
};

// ============================================================================
// EcsVulkanContext —— SpawnerSystem 所需的 Vulkan 资源句柄
// 由 VulkanApp 填充并注入 SpawnerSystem（注册后调用 setVulkanContext）。
// ============================================================================
struct EcsVulkanContext
{
    VkDevice             device          = VK_NULL_HANDLE;
    VkPhysicalDevice     physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool        commandPool     = VK_NULL_HANDLE;
    VkQueue              graphicsQueue   = VK_NULL_HANDLE;
    VkDescriptorSetLayout meshSetLayout  = VK_NULL_HANDLE;  // 3D 网格用
    VkDescriptorPool     meshPool        = VK_NULL_HANDLE;
    uint32_t             windowWidth     = 1280;
    uint32_t             windowHeight    = 720;
};

// ============================================================================
// CameraSystem —— 相机更新（向量/视图/投影）
//
// onUpdate：
//   - 处理自由飞行（flyEnabled）：WASD 平移、鼠标右键转头、Shift 加速
//   - 由 yaw/pitch 重算 front/right/up
//   - 写回 viewMatrix/projMatrix 供渲染系统读取
//
// 渲染系统通过 findPrimaryCamera() 取主相机 view/proj。
// ============================================================================
class CameraSystem : public System
{
public:
    void setInputState(const InputState* s) { m_input = s; }

    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        auto v = coord.view<Camera>();
        for (auto e : v)
        {
            auto& cam = v.get<Camera>(e);

            // 若有 Follow 组件且目标有效，优先走跟随逻辑（关闭 freeflight）
            bool hasFollow = coord.hasComponent<Follow>(e);
            if (hasFollow)
            {
                auto& f = coord.getComponent<Follow>(e);
                if (coord.valid(f.target) && coord.hasComponent<Transform>(f.target))
                {
                    const auto& targetTf = coord.getComponent<Transform>(f.target);
                    glm::vec3 desiredPos = targetTf.position + f.offset;
                    if (f.lerp > 0.0f && dt > 0.0f)
                    {
                        float t = std::min(1.0f, f.lerp * dt);
                        cam.position = glm::mix(cam.position, desiredPos, t);
                    }
                    else
                    {
                        cam.position = desiredPos;
                    }
                    if (f.lookAt)
                    {
                        // 令相机朝向 target，重算 yaw/pitch
                        glm::vec3 dir = targetTf.position - cam.position;
                        if (glm::length(dir) > 1e-5f)
                        {
                            dir = glm::normalize(dir);
                            // yaw = atan2(dir.z, dir.x) 转度；pitch = asin(dir.y)。
                            // 注意不能加负号：updateVectors 中 front.z = cos(pitch)*sin(yaw)，
                            // 加负号会让相机朝向与目标方向完全相反。
                            cam.yaw   = glm::degrees(std::atan2(dir.z, dir.x));
                            cam.pitch =  glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
                            cam.dirty = true;
                        }
                    }
                }
            }
            else if (cam.flyEnabled && m_input)
            {
                handleFly(cam, dt);
            }

            cam.updateVectors();
            if (m_input)
                cam.aspect = static_cast<float>(m_input->windowWidth) /
                             static_cast<float>(m_input->windowHeight ? m_input->windowHeight : 1);
            cam.viewMatrix = cam.view();
            cam.projMatrix = cam.projection();
            cam.dirty = false;
        }
    }

    // 查找主相机（isPrimary=true）；找不到返回 nullptr
    static const Camera* findPrimary(const Coordinator& coord)
    {
        auto v = coord.view<const Camera>();
        for (auto e : v)
        {
            auto& cam = v.get<const Camera>(e);
            if (cam.isPrimary) return &cam;
        }
        // 退化：取第一个相机
        for (auto e : v) return &v.get<const Camera>(e);
        return nullptr;
    }

    const char* name() const override { return "CameraSystem"; }

private:
    void handleFly(Camera& cam, float dt)
    {
        const float moveSpeed = 4.0f * (m_input->key(GLFW_KEY_LEFT_SHIFT) ? 3.0f : 1.0f);
        const float lookSensitivity = 0.15f;

        if (m_input->key(GLFW_KEY_W)) cam.position += cam.front * moveSpeed * dt;
        if (m_input->key(GLFW_KEY_S)) cam.position -= cam.front * moveSpeed * dt;
        if (m_input->key(GLFW_KEY_D)) cam.position += cam.right  * moveSpeed * dt;
        if (m_input->key(GLFW_KEY_A)) cam.position -= cam.right  * moveSpeed * dt;
        if (m_input->key(GLFW_KEY_SPACE))      cam.position += cam.worldUp * moveSpeed * dt;
        if (m_input->key(GLFW_KEY_LEFT_CONTROL)) cam.position -= cam.worldUp * moveSpeed * dt;

        // 仅在右键按下时转头（避免光标乱跑影响 UI）
        if (m_input->mouseRight)
        {
            cam.yaw   += static_cast<float>(m_input->mouseDeltaX) * lookSensitivity;
            cam.pitch -= static_cast<float>(m_input->mouseDeltaY) * lookSensitivity;
            cam.pitch = cam.pitch > 89.0f ? 89.0f : (cam.pitch < -89.0f ? -89.0f : cam.pitch);
            cam.dirty = true;
        }
    }

    const InputState* m_input = nullptr;
};

// ============================================================================
// HierarchySystem —— 父子层级世界变换计算
//
// 算法：先扫描所有 Parent 实体求最大祖先深度 maxDepth，再迭代 maxDepth 次。
//   每次迭代把"父世界变换 + 本地变换"合成"子世界变换"。
//   由于父级若自身也是 Parent，其变换在更早的迭代里已被更新，故 maxDepth
//   次扫描后任意深度的子链路都会被正确传播。
//
// 子实体世界 Transform 公式：
//   worldPos = parentPos + parentRotMatrix * (localPos * parentScale)
//   worldRot = parentRot + localRot
//   worldScale = parentScale * localScale
// 循环防御：向上走 parent 链时检测自身循环，超过 64 层中断
// ============================================================================
class HierarchySystem : public System
{
public:
    void onUpdate(Coordinator& coord, float /*dt*/) override
    {
        PROFILE_SCOPE(m_profiler, name());
        auto v = coord.view<Parent, Transform>();
        // 无实体时跳过（EnTT 3.13 view 无 empty()，用 begin==end 检测）
        if (v.begin() == v.end()) return;

        // ---- 1. 求 maxDepth ----
        int maxDepth = 0;
        for (auto e : v)
        {
            int d = 0;
            Entity cur = e;
            int guard = 0;
            while (coord.valid(cur) && coord.hasComponent<Parent>(cur) && guard++ < 64)
            {
                auto& p = coord.getComponent<Parent>(cur);
                if (p.parent == kNullEntity || !coord.valid(p.parent)) break;
                cur = p.parent;
                if (cur == e) break;  // 循环检测
                ++d;
            }
            if (d > maxDepth) maxDepth = d;
        }
        // 防御性下限：至少迭代 1 次（保留直接子实体的常驻行为）
        if (maxDepth < 1) maxDepth = 1;

        // ---- 2. 迭代传播 ----
        for (int pass = 0; pass < maxDepth; ++pass)
        {
            for (auto e : v)
            {
                auto& pr = v.get<Parent>(e);
                if (pr.parent == kNullEntity || !coord.valid(pr.parent)) continue;
                if (!coord.hasComponent<Transform>(pr.parent)) continue;

                auto& childTf = v.get<Transform>(e);
                const auto& parentTf = coord.getComponent<Transform>(pr.parent);

                // 父旋转矩阵（先 X 再 Y 再 Z，与 Transform::modelMatrix 一致）
                glm::mat4 rot(1.0f);
                rot = glm::rotate(rot, parentTf.rotationEuler.x, glm::vec3(1, 0, 0));
                rot = glm::rotate(rot, parentTf.rotationEuler.y, glm::vec3(0, 1, 0));
                rot = glm::rotate(rot, parentTf.rotationEuler.z, glm::vec3(0, 0, 1));

                glm::vec3 scaledLocal = pr.localPosition * parentTf.scale;
                childTf.position = parentTf.position + glm::mat3(rot) * scaledLocal;
                childTf.rotationEuler = parentTf.rotationEuler + pr.localRotationEuler;
                childTf.scale = parentTf.scale * pr.localScale;
            }
        }
    }
    const char* name() const override { return "HierarchySystem"; }
};

// ============================================================================
// InputSystem —— 键盘控制实体
//
// 方向键/WSAD 平移、Q/E 升降、R/F 自转。
// 注：相机自由飞行由 CameraSystem 处理；本系统只控制带 Input 组件的
//     场景实体（如玩家球）。
// ============================================================================
class InputSystem : public System
{
public:
    void setInputState(const InputState* s) { m_input = s; }

    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        if (!m_input) return;
        auto v = coord.view<Input, Transform>();
        for (auto e : v)
        {
            auto& inp = v.get<Input>(e);
            auto& tf  = v.get<Transform>(e);
            if (!inp.isPlayer) continue;

            // 方向键在 XZ 平面平移（WASD 留给相机自由飞行）
            glm::vec3 move(0.0f);
            if (m_input->key(GLFW_KEY_UP))    move.z -= 1.0f;
            if (m_input->key(GLFW_KEY_DOWN))  move.z += 1.0f;
            if (m_input->key(GLFW_KEY_LEFT))  move.x -= 1.0f;
            if (m_input->key(GLFW_KEY_RIGHT)) move.x += 1.0f;
            if (glm::length(move) > 0.0001f)
            {
                move = glm::normalize(move) * inp.moveSpeed * dt;
                tf.position += move;
            }
            // Q/E 升降
            if (m_input->key(GLFW_KEY_Q)) tf.position.y += inp.moveSpeed * dt;
            if (m_input->key(GLFW_KEY_E)) tf.position.y -= inp.moveSpeed * dt;
            // R/F 自转（绕 Y）
            float rot = glm::radians(inp.rotationSpeed) * dt;
            if (m_input->key(GLFW_KEY_R)) tf.rotationEuler.y += rot;
            if (m_input->key(GLFW_KEY_F)) tf.rotationEuler.y -= rot;
        }
    }
    const char* name() const override { return "InputSystem"; }

private:
    const InputState* m_input = nullptr;
};

// ============================================================================
// SpawnerSystem —— 定时生成实体（粒子）
//
// 每帧：
//   1. 扫描 SpawnerRef 按所属 spawner 分组计数 alive
//   2. 每个 Spawner 若 alive<max 且 timer 达 interval 则生成一个粒子
//
// 粒子为 2D Circle（NDC 空间），附 Movement（向外发散）+ Lifetime +
// SpawnerRef。被 LifetimeSystem 销毁时 SpawnerRef 随之消失，alive 自然下降。
// ============================================================================
class SpawnerSystem : public System
{
public:
    void setVulkanContext(const EcsVulkanContext* ctx) { m_ctx = ctx; }

    // 设置固定种子使粒子序列可复现（调试/回放用）
    void setSeed(uint32_t seed) { m_rng.seed(seed); }

    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        // ---- 1. 按生成器统计存活数 ----
        std::unordered_map<Entity, int> aliveCount;
        {
            auto v = coord.view<SpawnerRef>();
            for (auto e : v)
            {
                auto& ref = v.get<SpawnerRef>(e);
                if (ref.spawner != kNullEntity && coord.valid(ref.spawner))
                    ++aliveCount[ref.spawner];
            }
        }

        // ---- 2. 触发生成 ----
        auto v = coord.view<Spawner>();
        for (auto e : v)
        {
            auto& sp = v.get<Spawner>(e);
            sp.alive = aliveCount[e];  // 回写当前存活数
            sp.timer += dt;
            if (sp.alive >= sp.maxAlive) continue;
            // 长帧/掉帧时一次补齐应生成的粒子，避免剩余时间被清零丢弃
            while (sp.timer >= sp.interval)
            {
                sp.timer -= sp.interval;
                spawnParticle(coord, e, sp);
                if (++sp.alive >= sp.maxAlive) break;
            }
        }
    }
    const char* name() const override { return "SpawnerSystem"; }

private:
    void spawnParticle(Coordinator& coord, Entity spawner, const Spawner& sp)
    {
        if (!m_ctx) return;

        // ---- kCube：3D 立方体粒子（世界空间） ----
        if (sp.kind == Spawner::kCube)
        {
            spawnCube(coord, spawner, sp);
            return;
        }

        // ---- kParticle：2D 圆粒子（NDC 屏幕空间） ----
        // 随机方向（NDC 2D，spawnOrigin.x/y 为屏幕坐标，z 忽略）
        float ang = m_rng.angle();
        float r   = sp.spawnRadius * m_rng.range(0.3f, 1.0f);
        float px  = sp.spawnOrigin.x + cos(ang) * r;
        float py  = sp.spawnOrigin.y + sin(ang) * r;

        // 颜色微扰
        float j = m_rng.unit();
        float cr = sp.baseColor.r * (0.7f + 0.3f * j);
        float cg = sp.baseColor.g * (0.7f + 0.3f * j);
        float cb = sp.baseColor.b * (0.7f + 0.3f * j);

        auto circle = std::make_unique<Circle>(px, py, 0.03f, 12, cr, cg, cb);
        circle->upload(m_ctx->device, m_ctx->physicalDevice,
                       m_ctx->commandPool, m_ctx->graphicsQueue);

        // 向外发散速度（NDC/秒）
        float speed = m_rng.range(0.4f, 0.8f);
        glm::vec3 vel(cos(ang) * speed, sin(ang) * speed, 0.0f);

        Entity child = coord.createEntity();
        coord.addComponent<Shape2DComponent>(child, Shape2DComponent(std::move(circle)));
        coord.addComponent<Movement>(child, Movement{ vel, {0.0f, 0.0f, 0.0f} });
        coord.addComponent<Lifetime>(child, Lifetime{ sp.spawnLifetime });
        coord.addComponent<SpawnerRef>(child, SpawnerRef{ spawner });
    }

    // 生成 3D 立方体粒子：球内随机位置 + 随机大小 + 颜色微扰 + 随机速度
    // 向外发散 + 重力下沉 + 随机自旋 + 生命周期（由 LifetimeSystem 回收）
    void spawnCube(Coordinator& coord, Entity spawner, const Spawner& sp)
    {
        glm::vec3 dir = m_rng.onUnitSphere();
        glm::vec3 pos = sp.spawnOrigin +
                        dir * (sp.spawnRadius * m_rng.range(0.2f, 1.0f));
        float size = 0.08f * m_rng.range(0.5f, 1.6f);

        // 颜色微扰（保持基色明暗随机）
        float j  = m_rng.range(0.75f, 1.25f);
        float cr = std::clamp(sp.baseColor.r * j, 0.0f, 1.0f);
        float cg = std::clamp(sp.baseColor.g * j, 0.0f, 1.0f);
        float cb = std::clamp(sp.baseColor.b * j, 0.0f, 1.0f);

        // 单位立方体 + Transform.scale 控制大小（网格单位化，便于复用）
        auto cube = std::make_unique<Cube>(1.0f, cr, cg, cb);
        cube->upload(m_ctx->device, m_ctx->physicalDevice,
                     m_ctx->commandPool, m_ctx->graphicsQueue,
                     m_ctx->meshSetLayout, m_ctx->meshPool);

        // 速度：沿随机方向发散 + 重力下沉 + 随机自旋
        float speed  = m_rng.range(0.5f, 1.4f);
        glm::vec3 vel  = dir * speed;
        glm::vec3 spin = m_rng.onUnitSphere() * m_rng.range(1.0f, 3.5f);
        glm::vec3 g    = glm::vec3(0.0f, -1.5f, 0.0f);  // 重力

        Entity child = coord.createEntity();
        coord.addComponent<Transform>(child,
            Transform{ pos, glm::vec3(0.0f), glm::vec3(size) });
        coord.addComponent<Mesh3DComponent>(child, Mesh3DComponent(std::move(cube)));
        coord.addComponent<Movement>(child, Movement{ vel, spin, g });
        coord.addComponent<Lifetime>(child, Lifetime{ sp.spawnLifetime });
        coord.addComponent<SpawnerRef>(child, SpawnerRef{ spawner });
    }

    const EcsVulkanContext* m_ctx = nullptr;
    Rng m_rng;
};

// ============================================================================
// DebugSystem —— 调试信息显示
//
// 每隔 0.5 秒查找名为 "debug_text" 的实体（须有 TextComponent），写入
// 当前 FPS / 帧数 / 实体数。如果场景无此实体则静默跳过。
//
// 用法：
//   1. 在场景中创建：auto e = coord.entity("debug_text").with<TextComponent>(...)
//   2. 在 VulkanApp::bindEcsExternalContexts 中给 DebugSystem 注入 time/ctx
// ============================================================================
class DebugSystem : public System
{
public:
    void setTimeState(const TimeState* t) { m_time = t; }
    void setVulkanContext(const EcsVulkanContext* ctx) { m_ctx = ctx; }

    void onUpdate(Coordinator& coord, float /*dt*/) override
    {
        PROFILE_SCOPE(m_profiler, name());
        if (!m_time || !m_ctx) return;
        // 节流：每 0.5 秒刷新一次（避免文字闪烁 + 减少 upload 开销）
        if (m_time->elapsed - m_lastRefresh < 0.5f) return;
        m_lastRefresh = m_time->elapsed;

        Entity e = coord.findByName("debug_text");
        if (e == kNullEntity || !coord.hasComponent<TextComponent>(e)) return;
        auto& tc = coord.getComponent<TextComponent>(e);
        if (!tc.renderer) return;

        size_t total = coord.entityCount();

        std::string text;
        char line[160];

        // 第 1 行：基础运行信息
        std::snprintf(line, sizeof(line),
            "FPS:%5.1f  Frame:%llu  Entities:%zu",
            m_time->fps,
            static_cast<unsigned long long>(m_time->frameCount),
            total);
        text += line;

        // 第 2 行起：各系统耗时（snapshot 已按平均耗时降序，最多展示 8 行）
        if (m_profiler)
        {
            text += "\n-- System 耗时 (avg/max ms) --";
            const auto snaps = m_profiler->snapshot();
            int shown = 0;
            for (const auto& s : snaps)
            {
                if (shown >= 8) break;
                std::snprintf(line, sizeof(line),
                    "\n%-16s %7.3f / %7.3f",
                    s.name.c_str(), s.avgMs, s.maxMs);
                text += line;
                ++shown;
            }
            std::snprintf(line, sizeof(line), "\nFrame avg: %.2f ms",
                          m_profiler->getAvgFrameMs());
            text += line;
        }

        tc.renderer->setText(text);
        tc.renderer->upload(m_ctx->device, m_ctx->physicalDevice,
                             m_ctx->commandPool, m_ctx->graphicsQueue);
        tc.dirty = true;
    }
    const char* name() const override { return "DebugSystem"; }

private:
    const TimeState*        m_time = nullptr;
    const EcsVulkanContext* m_ctx  = nullptr;
    float m_lastRefresh = -1.0f;
};

// ============================================================================
// LightingSystem —— 灯光系统
//
// 每帧扫描所有 Light 组件，识别主方向光并缓存指针。
// RenderSystem3D 可通过 primaryLight() 查询本帧主光数据，注入到 shader
// 的 pushConstants（如未来 mesh3d.frag 扩展支持 lightDir/lightColor）。
//
// 当前版本是"逻辑层灯光状态收集器"——shader 暂未支持光照，但本系统
// 已经能驱动 DebugSystem 显示主光方向、供未来扩展。
// ============================================================================
class LightingSystem : public System
{
public:
    void onUpdate(Coordinator& coord, float /*dt*/) override
    {
        PROFILE_SCOPE(m_profiler, name());
        m_primaryLight = nullptr;
        m_lightCount   = 0;
        auto v = coord.view<Light>();
        for (auto e : v)
        {
            auto& l = v.get<Light>(e);
            ++m_lightCount;
            // 第一个 isPrimary 的方向光作为主光
            if (l.isPrimary && l.kind == Light::kDirectional && !m_primaryLight)
            {
                m_primaryLight = &l;
            }
        }
        // 退化：若没有标记 primary 的方向光，取第一个方向光
        if (!m_primaryLight)
        {
            for (auto e : v)
            {
                auto& l = v.get<Light>(e);
                if (l.kind == Light::kDirectional) { m_primaryLight = &l; break; }
            }
        }
    }

    const Light* primaryLight() const { return m_primaryLight; }
    size_t       lightCount() const { return m_lightCount; }

    const char* name() const override { return "LightingSystem"; }

private:
    const Light* m_primaryLight = nullptr;
    size_t       m_lightCount   = 0;
};

// ============================================================================
// ColliderSystem —— AABB 碰撞检测（v1.7：enter/stay/exit 三态边沿事件 + 双向修正）
//
// 算法：O(n²) 双循环比较所有 Collider+Transform 实体对。
//   - 静态场景（< 100 实体）足够；后续如需加速可改 BVH/SAP
//   - AABB 重叠测试：|c1-c2| < halfExtents1+halfExtents2（每轴独立）
//   - 重叠时计算穿透深度和法线（最小重叠轴方向）
//   - 事件三态（与 Unity 等引擎一致）：
//       * CollisionEnterEvent：上一帧不重叠 → 本帧重叠（边沿触发一次）
//       * CollisionEvent      ：持续重叠（每帧，向后兼容）
//       * CollisionExitEvent  ：上一帧重叠 → 本帧不重叠（边沿触发一次）
//     由 EventBus 发布；订阅者可据此实现"踏入/离开区域"类触发逻辑。
//   - isTrigger=false 时进行双向位置修正：按静态标志把穿透量分给可动物体
//     （双方都可动各推一半；一方静态则另一方全量退让），比旧版"只推 b"
//     更对称、无抖动。
//
// 集成方式：在 registerEcsSystems 中排在 MovementSystem 之后、HierarchySystem
// 之前，确保使用本帧最新位置。
// ============================================================================
class ColliderSystem : public System
{
public:
    void onUpdate(Coordinator& coord, float /*dt*/) override
    {
        PROFILE_SCOPE(m_profiler, name());
        // 清空上一帧的 colliding 标记
        auto clearView = coord.view<Collider>();
        for (auto e : clearView) clearView.get<Collider>(e).colliding = false;

        auto v = coord.view<Transform, Collider>();
        // 收集到 vector 便于 O(n²) 双循环
        std::vector<Entity> ents;
        ents.reserve(16);
        for (auto e : v) ents.push_back(e);

        // 本帧重叠对 → 重叠数据（normal/penetration）
        std::map<Key, OverlapData> cur;

        for (size_t i = 0; i < ents.size(); ++i)
        {
            for (size_t j = i + 1; j < ents.size(); ++j)
            {
                Entity ea = ents[i], eb = ents[j];
                auto& ta = v.get<Transform>(ea);
                auto& tb = v.get<Transform>(eb);
                auto& ca = v.get<Collider>(ea);
                auto& cb = v.get<Collider>(eb);

                // AABB 重叠测试（每轴独立）
                glm::vec3 d = tb.position - ta.position;
                float ox = ca.halfExtents.x + cb.halfExtents.x - std::abs(d.x);
                if (ox <= 0.0f) continue;
                float oy = ca.halfExtents.y + cb.halfExtents.y - std::abs(d.y);
                if (oy <= 0.0f) continue;
                float oz = ca.halfExtents.z + cb.halfExtents.z - std::abs(d.z);
                if (oz <= 0.0f) continue;

                // 找最小重叠轴作为法线
                glm::vec3 normal(0, 1, 0);
                float penetration = ox;
                if (oy < penetration) { penetration = oy; normal = {0, 1, 0}; }
                if (oz < penetration) { penetration = oz; normal = {0, 0, 1}; }
                if (d.x < 0 && normal.x != 0) normal.x = -normal.x;
                if (d.y < 0 && normal.y != 0) normal.y = -normal.y;
                if (d.z < 0 && normal.z != 0) normal.z = -normal.z;

                ca.colliding = true;
                cb.colliding = true;
                cur.emplace(Key(ea, eb), OverlapData{ normal, penetration });

                // 持续重叠事件（stay，向后兼容）
                coord.events().publish<CollisionEvent>({ ea, eb, normal, penetration });

                // 双向位置修正（仅非触发器：触发器不阻挡物体，只发事件）
                if (!ca.isTrigger && !cb.isTrigger)
                    resolvePenetration(ta, tb, ca, cb, normal, penetration);
            }
        }

        // ---- 边沿事件 ----
        // enter：本帧重叠但上一帧没有 → 只在状态跳变那一帧触发
        for (const auto& [key, data] : cur)
        {
            if (m_prevOverlaps.find(key) == m_prevOverlaps.end())
            {
                coord.events().publish<CollisionEnterEvent>(
                    { key.first, key.second, data.normal, data.penetration });
            }
        }
        // exit：上一帧重叠但本帧没有 → 只在状态跳变那一帧触发
        for (const auto& key : m_prevOverlaps)
        {
            if (cur.find(key) == cur.end())
            {
                coord.events().publish<CollisionExitEvent>({ key.first, key.second });
            }
        }
        // 更新上一帧重叠集合（供下一帧做边沿比较）
        m_prevOverlaps.clear();
        for (const auto& [key, data] : cur) m_prevOverlaps.insert(key);
    }

    const char* name() const override { return "ColliderSystem"; }

private:
    using Key = std::pair<Entity, Entity>;
    struct OverlapData
    {
        glm::vec3 normal{0, 1, 0};
        float penetration = 0.0f;
    };

    // 双向穿透分离：按静态标志把穿透量分给可动物体
    // normal 方向为 a→b；a 沿 -normal 退让，b 沿 +normal 退让
    static void resolvePenetration(Transform& ta, Transform& tb,
                                   const Collider& ca, const Collider& cb,
                                   const glm::vec3& normal, float penetration)
    {
        const bool aMovable = !ca.isStatic;
        const bool bMovable = !cb.isStatic;
        if (aMovable && bMovable)
        {
            ta.position -= normal * (penetration * 0.5f);
            tb.position += normal * (penetration * 0.5f);
        }
        else if (aMovable)
        {
            ta.position -= normal * penetration;   // b 静态：a 全量退让
        }
        else if (bMovable)
        {
            tb.position += normal * penetration;   // a 静态：b 全量退让
        }
    }

    std::set<Key> m_prevOverlaps;  // 上一帧重叠对集合（边沿检测状态）
};

// ============================================================================
// TriggerSystem —— 区域触发器（v1.0.2）
//
// 每帧检测：带 Transform + TriggerZone 的区域（AABB/球），
// 收集区域外所有带 Transform 的实体做包含测试；
// 进出区域（边沿一次）发布 TriggerEnterEvent / TriggerExitEvent，
// 并维护 TriggerZone::inside 集合供外部查询。
// 与 ColliderSystem 互补：纯几何查询，无需目标实体挂 Collider。
// ============================================================================
class TriggerSystem : public System
{
public:
    void onUpdate(Coordinator& coord, float dt) override
    {
        PROFILE_SCOPE(m_profiler, name());
        (void)dt;
        auto zones = coord.view<Transform, TriggerZone>();
        for (auto z : zones)
        {
            const auto& tf   = zones.get<Transform>(z);
            auto&       zone = zones.get<TriggerZone>(z);

            if (!zone.active)
            {
                // 区域关闭：全部视为退出
                for (Entity e : zone.inside)
                    coord.events().publish(TriggerExitEvent{ z, e });
                zone.inside.clear();
                continue;
            }

            // 收集当前帧在区域内的实体
            std::vector<Entity> current;
            auto others = coord.view<Transform>();
            for (auto e : others)
            {
                if (e == z) continue;
                const auto& otf = others.get<Transform>(e);
                const glm::vec3 d = otf.position - tf.position;
                bool hit = (zone.kind == TriggerZone::kSphere)
                               ? (glm::dot(d, d) <= zone.radius * zone.radius)
                               : (std::fabs(d.x) <= zone.halfExtents.x &&
                                  std::fabs(d.y) <= zone.halfExtents.y &&
                                  std::fabs(d.z) <= zone.halfExtents.z);
                if (hit) current.push_back(e);
            }

            // 进入（边沿一次）
            for (Entity e : current)
            {
                if (std::find(zone.inside.begin(), zone.inside.end(), e) == zone.inside.end())
                {
                    coord.events().publish(TriggerEnterEvent{ z, e });
                    zone.inside.push_back(e);
                }
            }
            // 退出（边沿一次）
            for (auto it = zone.inside.begin(); it != zone.inside.end(); )
            {
                if (std::find(current.begin(), current.end(), *it) == current.end())
                {
                    coord.events().publish(TriggerExitEvent{ z, *it });
                    it = zone.inside.erase(it);
                }
                else { ++it; }
            }
        }
    }

    const char* name() const override { return "TriggerSystem"; }
};

} // namespace ecs
