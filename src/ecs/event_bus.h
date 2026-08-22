/**
 * event_bus.h —— ECS 事件总线（v1.0.2 起复用引擎级完整事件系统）
 *
 * 底层实现见 engine/event_system.h（events::EventBus）：
 *   - 类型安全发布/订阅（subscribe / subscribeOnce）
 *   - 订阅优先级（priority 越小越先执行）
 *   - 延迟派发队列（enqueue / dispatch）
 *
 * 兼容性：`ecs::EventBus` / `ecs::HandlerId` 为类型别名，旧调用点无需修改。
 * 独立实例由 Coordinator 持有；引擎级全局事件用 events::system()。
 */
#pragma once

#include "engine/event_system.h"

namespace ecs {

using HandlerId = events::HandlerId;
using EventBus  = events::EventBus;

} // namespace ecs
