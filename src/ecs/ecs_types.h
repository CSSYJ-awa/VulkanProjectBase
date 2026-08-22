/**
 * ecs_types.h —— ECS 核心类型别名
 *
 * 把 EnTT 的底层类型封装为项目内统一别名，便于：
 *   - 隔离 EnTT API 升级影响（替换库时只改这里）
 *   - 给实体加一个稳定的句柄类型 Entity，避免业务代码到处写 entt::entity
 *
 * 实体本质是 int32 索引（entt::entity = entt::id_type 包装），
 * 不持有任何数据；数据全部由组件（Component）承载。
 */
#pragma once

#include <entt/entt.hpp>
#include <cstdint>

namespace ecs {

// 实体句柄：本质是 entt::entity 的别名，是一个稳定的 ID
using Entity       = entt::entity;
using Registry     = entt::registry;
using EntityVersion = entt::id_type;

// 空实体哨兵（entt::null）
inline constexpr Entity kNullEntity = entt::null;

} // namespace ecs
