/**
 * components.h —— ECS 组件定义
 *
 * 设计原则：
 *   - 组件是**纯数据**（POD/聚合体），不包含逻辑
 *   - 一个实体通过 emplace<Component>() 挂载多个组件
 *   - 系统通过 entt::view 按组件集合查询实体
 *
 * 与现有渲染系统的衔接：
 *   - MeshComponent / Shape2DComponent / TextComponent 直接持有
 *     std::unique_ptr<Mesh3D/Shape/TextRenderer>，复用现有 GPU 资源管理
 *   - TransformComponent 提供 3D 模型矩阵（位置/旋转/缩放）
 *   - 系统（RenderSystem3D 等）在每帧 update() 中调用对应 drawVBOOnly
 *
 * 之所以用 unique_ptr 包装：Mesh3D/Shape/TextRenderer 不可拷贝、
 * 不可默认构造（持有 Vulkan 资源），EnTT 的 sparse_set 需要组件可移动
 * 但不要求可拷贝；unique_ptr 移动开销 O(1)，性能与裸指针等同。
 */
#pragma once

#include "ecs_types.h"
#include "../shapes/shape.h"
#include "../geometry3d/mesh3d.h"
#include "../text/text_renderer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>

namespace ecs {

// ============================================================================
// Transform —— 3D 变换（位置/旋转/缩放）
// ============================================================================
struct Transform
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEuler{0.0f, 0.0f, 0.0f};  // 欧拉角（弧度）
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    // 计算模型矩阵：T * Rx * Ry * Rz * S
    glm::mat4 modelMatrix() const
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, rotationEuler.x, glm::vec3(1, 0, 0));
        m = glm::rotate(m, rotationEuler.y, glm::vec3(0, 1, 0));
        m = glm::rotate(m, rotationEuler.z, glm::vec3(0, 0, 1));
        m = glm::scale(m, scale);
        return m;
    }
};

// ============================================================================
// Movement —— 运动（线速度 + 角速度 + 线加速度）
// 系统每帧更新 Transform：
//   velocity += acceleration * dt
//   position += velocity * dt;  rotationEuler += angular * dt
// acceleration 可用于重力、风力、力场等（单位/秒²），默认 0 不影响旧用法。
// ============================================================================
struct Movement
{
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};  // 弧度/秒
    glm::vec3 acceleration{0.0f};     // 线加速度（单位/秒²）
};

// ============================================================================
// Mesh3DComponent —— 3D 网格组件
// 持有 Mesh3D 实例（Cube/Polyhedron/自定义），由 RenderSystem3D 渲染
// ============================================================================
struct Mesh3DComponent
{
    std::unique_ptr<Mesh3D> mesh;
    bool dirty = false;  // 标记 Transform 已变化，需 setModel

    // 便利构造：转移所有权
    explicit Mesh3DComponent(std::unique_ptr<Mesh3D> m = nullptr)
        : mesh(std::move(m)) {}

    // EnTT sparse_set 需要可移动构造（unique_ptr 默认满足）
    Mesh3DComponent(Mesh3DComponent&&) noexcept = default;
    Mesh3DComponent& operator=(Mesh3DComponent&&) noexcept = default;
    Mesh3DComponent(const Mesh3DComponent&) = delete;
    Mesh3DComponent& operator=(const Mesh3DComponent&) = delete;
};

// ============================================================================
// Shape2DComponent —— 2D 图元组件
// 持有 Shape 实例（Line/Triangle/Circle/...），由 RenderSystem2D 渲染
// ============================================================================
struct Shape2DComponent
{
    std::unique_ptr<Shape> shape;

    explicit Shape2DComponent(std::unique_ptr<Shape> s = nullptr)
        : shape(std::move(s)) {}

    Shape2DComponent(Shape2DComponent&&) noexcept = default;
    Shape2DComponent& operator=(Shape2DComponent&&) noexcept = default;
    Shape2DComponent(const Shape2DComponent&) = delete;
    Shape2DComponent& operator=(const Shape2DComponent&) = delete;
};

// ============================================================================
// TextComponent —— 文字组件
// 持有 TextRenderer 实例，由 RenderSystemText 渲染
// ============================================================================
struct TextComponent
{
    std::unique_ptr<TextRenderer> renderer;
    bool dirty = false;  // 文字内容变化时置 true，下次 update 重新 upload

    explicit TextComponent(std::unique_ptr<TextRenderer> t = nullptr)
        : renderer(std::move(t)) {}

    TextComponent(TextComponent&&) noexcept = default;
    TextComponent& operator=(TextComponent&&) noexcept = default;
    TextComponent(const TextComponent&) = delete;
    TextComponent& operator=(const TextComponent&) = delete;
};

// ============================================================================
// Tag —— 字符串标签（用于按名查找实体）
// ============================================================================
struct Tag
{
    std::string name;
};

// ============================================================================
// Lifetime —— 生命周期（秒；<=0 表示无限制）
// LifetimeSystem 每帧减少 remaining，<=0 时销毁实体
// ============================================================================
struct Lifetime
{
    float remaining = -1.0f;  // < 0 表示永久
};

// ============================================================================
// Camera —— ECS 驱动的相机
//
// 拥有位置/朝向/视锥体参数，由 CameraSystem 每帧根据 yaw/pitch 计算
// front/right/up 向量并更新 viewMatrix/projMatrix。
// isPrimary 标记主相机：渲染系统优先使用主相机的 view/proj。
// flyEnabled=true 时进入自由飞行模式（WASD 移动 + 鼠标右键转头）。
// ============================================================================
struct Camera
{
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    float yaw   = -90.0f;   // 偏航角（度）：-90 正对 -Z
    float pitch = 0.0f;     // 俯仰角（度）
    float fov   = 45.0f;    // 视野（度）
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ  = 100.0f;

    bool isPrimary  = true;
    bool flyEnabled = false;

    // 由 CameraSystem 每帧更新（勿手动写）
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projMatrix{1.0f};
    bool dirty = true;  // yaw/pitch/fov 变化时置 true

    // 根据 yaw/pitch 重算 front/right/up（CameraSystem 调用）
    void updateVectors()
    {
        // 夹紧 pitch 防止 ±90° 时 front 与 worldUp 平行，cross=0 → normalize 产生 NaN
        float p = glm::clamp(pitch, -89.9f, 89.9f);
        float cy = glm::radians(yaw), cp = glm::radians(p);
        front = glm::normalize(glm::vec3(
            cos(cp) * cos(cy),
            sin(cp),
            cos(cp) * sin(cy)));
        right = glm::normalize(glm::cross(front, worldUp));
        up    = glm::normalize(glm::cross(right, front));
    }
    glm::mat4 view() const
    {
        return glm::lookAt(position, position + front, up);
    }
    glm::mat4 projection() const
    {
        glm::mat4 p = glm::perspective(glm::radians(fov), aspect, nearZ, farZ);
        p[1][1] *= -1.0f;  // Vulkan Y 翻转
        return p;
    }
};

// ============================================================================
// Parent —— 父子层级关系
//
// 父实体的 Transform 视为世界变换；本组件记录相对父节点的本地变换。
// HierarchySystem 每帧计算子实体世界 Transform：
//   worldPos = parentPos + parentRotMatrix * (localPos * parentScale)
//   worldRot = parentRot + localRot
//   worldScale = parentScale * localScale
// 并写回子实体的 Transform（世界空间）。
// 约定：父实体需先于子实体创建，系统按两次扫描保证三级层级稳定。
// ============================================================================
struct Parent
{
    Entity parent = kNullEntity;
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localRotationEuler{0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};
};

// ============================================================================
// Input —— 可控实体标记
//
// 挂载此组件的实体可被 InputSystem 操控：
//   方向键/WSAD：平移（按 moveSpeed）
//   Q/E：升降
//   R/F：自转
// isPlayer 标记"玩家"实体（键盘控制位置）；否则只读方向不影响。
// ============================================================================
struct Input
{
    float moveSpeed     = 3.0f;  // 单位/秒
    float rotationSpeed = 90.0f; // 度/秒
    bool  isPlayer      = true;
};

// ============================================================================
// Spawner —— 生成器：定时创建子实体
//
// SpawnerSystem 每帧累加 timer，达到 interval 时创建一个实体：
//   kind=kParticle：在 spawnOrigin 半径 spawnRadius 球内随机位置生成小 2D 圆
//                   附 Movement（向外发散速度）+ Lifetime（spawnLifetime）
//   kind=kCube：生成小型 3D 立方体（需 EcsVulkanContext 提供 3D 上传资源）
// alive 记录已存活数量，达到 maxAlive 则暂停生成。
// ============================================================================
struct Spawner
{
    enum Kind { kParticle, kCube };
    Kind  kind = kParticle;
    float interval       = 0.3f;
    float timer          = 0.0f;
    int   maxAlive       = 50;
    int   alive          = 0;
    float spawnLifetime  = 2.0f;
    glm::vec3 spawnOrigin{0.0f, 0.0f, 0.0f};
    float spawnRadius    = 0.5f;
    glm::vec3 baseColor  {0.9f, 0.6f, 0.2f};
};

// ============================================================================
// SpawnerRef —— 标记"由某生成器产生"，用于精确计数存活数量
// SpawnerSystem 每帧扫描本组件，按 spawner 字段分组计数；子实体被
// LifetimeSystem 销毁时 SpawnerRef 随之消失，计数自然下降。
// ============================================================================
struct SpawnerRef
{
    Entity spawner = kNullEntity;
};

// ============================================================================
// CollisionEvent —— 碰撞事件（由 ColliderSystem 通过 EventBus 发布）
//
// 订阅者可订阅此事件实现"碰撞响应"：
//   coord.events().subscribe<ecs::CollisionEvent>([&](const ecs::CollisionEvent& ev){
//       if (coord.hasComponent<Tag>(ev.a) && coord.hasComponent<Tag>(ev.b)) {
//           // 处理两个实体碰撞逻辑
//       }
//   });
// ============================================================================
struct CollisionEvent
{
    Entity a;                  // 主动方（先扫描到的）
    Entity b;                  // 被动方
    glm::vec3 normal{0,1,0};   // 碰撞法线（a→b 方向）
    float penetration = 0.0f;  // 穿透深度
};

// ============================================================================
// CollisionEnterEvent —— 进入碰撞（v1.7 边沿事件）
//
// 语义：上一帧这对实体未重叠，本帧开始重叠，仅在状态跳变的那一帧触发一次
//（不会每帧刷屏）。适合实现"踏入区域"类逻辑：门打开、触发器、加分等。
// normal/penetration 为进入那一刻的重叠数据（a→b 方向）。
// ============================================================================
struct CollisionEnterEvent
{
    Entity a;
    Entity b;
    glm::vec3 normal{0,1,0};
    float penetration = 0.0f;
};

// ============================================================================
// CollisionExitEvent —— 退出碰撞（v1.7 边沿事件）
//
// 语义：上一帧这对实体重叠，本帧不再重叠，仅在状态跳变的那一帧触发一次。
// 与 CollisionEnterEvent 配对使用，实现"离开区域"类逻辑。
// ============================================================================
struct CollisionExitEvent
{
    Entity a;
    Entity b;
};

// ============================================================================
// Light —— 灯光组件（方向光 / 点光源 / 聚光灯）
//
// LightingSystem 每帧收集所有 Light 组件，统一计算场景环境光 + 主方向光，
// 通过 PushConstants 或 UBO 注入到 mesh3d.frag（需 shader 支持）。
//
// 当前版本简化：仅用于"逻辑上的灯光数据 + 调试可视化"。
// 后续接入 shader 后，Mesh3DComponent 可在 RenderSystem3D 中查询主光
// 注入 pushConstants.lightDir/lightColor。
// ============================================================================
struct Light
{
    enum Kind { kDirectional, kPoint, kSpot };
    Kind  kind     = kDirectional;
    // 方向光：direction 单位向量（世界空间）
    // 点光/聚光：direction 为照射方向（聚光主轴）
    glm::vec3 direction{0.0f, -1.0f, 0.3f};
    // 强度（0~1+），颜色（线性 RGB）
    float     intensity = 1.0f;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    // 点光衰减半径（仅 kPoint/kSpot 用）
    float     range     = 10.0f;
    // 聚光锥角度（度，仅 kSpot 用）
    float     spotAngle = 45.0f;
    // 是否为主光（场景里通常只有 1 个 isPrimary=true 的方向光）
    bool      isPrimary = false;
};

// ============================================================================
// Collider —— AABB 碰撞盒
//
// ColliderSystem 每帧检测所有带 Collider + Transform 的实体对，
// 重叠时通过 EventBus 发布 CollisionEvent 事件（订阅者可执行响应逻辑）。
// isTrigger=true 时不阻挡物理，仅触发事件；否则可以做简单"分离"处理。
// halfExtents 是半盒大小（中心默认为 Transform.position）。
// ============================================================================
struct Collider
{
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f};
    bool      isTrigger   = false;
    bool      isStatic    = false;   // 静态碰撞盒（不参与位移修正，仅作触发源）
    // 由 ColliderSystem 写入：当前帧是否在碰撞中
    bool      colliding   = false;
};

// ============================================================================
// Follow —— 相机/实体跟随目标
//
// 与 Camera 组件配合使用：CameraSystem 在 freeflight 关闭时，
// 若实体同时有 Follow 组件，则把相机 position 设为目标 position + offset，
// 并按 lerp 平滑趋近；lookAt 目标使相机始终朝向被跟随实体。
// 与 Movement 不冲突：Follow 只改 position + 朝向。
// ============================================================================
struct Follow
{
    Entity    target   = kNullEntity;       // 跟随目标（须有 Transform）
    glm::vec3 offset   {0.0f, 3.0f, 8.0f}; // 相机相对目标的偏移
    float     lerp     = 4.0f;             // 平滑系数（越大越紧跟，0=不平滑）
    bool      lookAt   = true;             // 是否始终朝向目标
};

// ============================================================================
// TriggerZone —— 区域触发器（v1.0.2）
//
// 由 TriggerSystem 每帧检测：区域中心 = Transform.position，
// 区域内（AABB 半盒 / 球半径）所有带 Transform 的实体（不含区域自身），
// 进出区域时通过 EventBus 发布 TriggerEnterEvent / TriggerExitEvent（边沿一次）。
// 与 Collider 互补：无需对实体挂 Collider，纯几何查询。
// ============================================================================
struct TriggerZone
{
    enum Kind { kAABB, kSphere };
    Kind       kind        = kAABB;
    glm::vec3  halfExtents {1.0f, 1.0f, 1.0f};   // AABB 半盒
    float      radius      = 1.0f;               // Sphere 半径
    bool       active      = true;               // 关闭后不检测（已在内实体视为退出）
    // 由 TriggerSystem 维护：当前区域内的实体集合（可供外部查询）
    std::vector<Entity> inside;
};

// 区域触发事件（边沿一次）
struct TriggerEnterEvent { Entity zone; Entity other; };
struct TriggerExitEvent  { Entity zone; Entity other; };

} // namespace ecs
