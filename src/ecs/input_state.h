/**
 * input_state.h —— ECS 输入状态共享结构 + InputMapper 动作映射
 *
 * InputState 由 VulkanApp 的 GLFW 回调填充，InputSystem / CameraSystem 读取。
 * 通过指针注入系统（registerSystem 后调用 setInputState），保持 System
 * onUpdate(Coordinator&, float) 接口不变。
 *
 * InputMapper（v1.6 新增）：
 *   - 把"按键 → 动作名"映射抽象出来，业务代码不再依赖具体 GLFW 键码
 *   - 支持同一动作绑多个键（如 jump = SPACE 或 GamepadA）
 *   - 用户可运行时修改映射（重绑按键），实现自定义化
 *   - 提供 justPressed / justReleased 边沿检测（需要每帧末尾 endFrame）
 *
 * 设计权衡：
 *   - InputState 用固定大小数组而非 std::unordered_map，O(1) 查询，零分配
 *   - 键值范围覆盖 GLFW 常用键码（GLFW_KEY_LAST ≈ 348），数组取 400 留余量
 *   - 鼠标 delta 每帧消费后由 VulkanApp::run 重置，避免累积
 *   - InputMapper 用 std::unordered_map<string, vector<int>>，少量动作足够
 */
#pragma once

#include <array>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// 前置：GLFW 键码为 int，无需包含 glfw3.h（避免与 Vulkan 头冲突）
namespace ecs {

// ============================================================================
// InputState —— 原始键鼠状态（GLFW 回调直接填充）
// ============================================================================
struct InputState
{
    // 键盘按下状态（true=按下）。索引为 GLFW 键码
    std::array<bool, 400> keys{};

    // 鼠标
    double mouseX = 0.0, mouseY = 0.0;
    double mouseDeltaX = 0.0, mouseDeltaY = 0.0;
    bool mouseLeft = false, mouseRight = false, mouseMiddle = false;

    // 窗口尺寸（供相机计算 aspect / 鼠标归一化）
    int windowWidth = 1280, windowHeight = 720;

    // 查询某键是否按下（越界返回 false）
    bool key(int k) const
    {
        return k >= 0 && k < static_cast<int>(keys.size()) && keys[k];
    }

    // 每帧开头调用：把 delta 清零（位移已由系统消费）
    void clearDeltas()
    {
        mouseDeltaX = 0.0;
        mouseDeltaY = 0.0;
    }
};

// ============================================================================
// InputMapper —— 动作映射 + 边沿检测
//
// 用法：
//   InputMapper mapper;
//   mapper.bind("jump",      GLFW_KEY_SPACE);
//   mapper.bind("forward",  GLFW_KEY_W);
//   mapper.bind("forward",  GLFW_KEY_UP);   // 同一动作绑多键
//
//   // 在系统里：
//   if (mapper.action("forward")) player.move(z -= 1);
//   if (mapper.justPressed("jump")) sound.play("jump");
//
//   // 每帧末尾必须调用：
//   mapper.endFrame();  // 把"边沿"状态推进
//
// 与 InputState 区别：
//   - InputState 是"原始键状态"（GLFW 直接填）
//   - InputMapper 是"语义动作"（业务层用，可重绑）
// ============================================================================
class InputMapper
{
public:
    // 绑定一个键到动作（同一动作可多次 bind 累加多键）
    void bind(const std::string& action, int key)
    {
        m_actionToKeys[action].push_back(key);
        // 反向索引：key → 动作列表（一钥可能触发多个动作）
        m_keyToActions[key].push_back(action);
    }

    // 解绑某动作的所有键
    void unbind(const std::string& action)
    {
        auto it = m_actionToKeys.find(action);
        if (it == m_actionToKeys.end()) return;
        for (int k : it->second)
        {
            if (auto* vec = findOrNull(m_keyToActions, k))
            {
                vec->erase(std::remove(vec->begin(), vec->end(), action), vec->end());
            }
        }
        m_actionToKeys.erase(it);
    }

    // 重新绑定：清空原绑定后绑新键
    void rebind(const std::string& action, int newKey)
    {
        unbind(action);
        bind(action, newKey);
    }

    // 查询动作当前是否激活（任一绑定键按下）
    bool action(const std::string& name) const
    {
        auto it = m_actionToKeys.find(name);
        if (it == m_actionToKeys.end()) return false;
        for (int k : it->second)
            if (m_state && m_state->key(k)) return true;
        return false;
    }

    // 边沿检测：本帧刚按下（true 表示从 false→true 的过渡）
    // 需每帧末尾调用 endFrame() 推进状态
    bool justPressed(const std::string& name) const
    {
        auto it = m_justPressed.find(name);
        return it != m_justPressed.end() && it->second;
    }

    bool justReleased(const std::string& name) const
    {
        auto it = m_justReleased.find(name);
        return it != m_justReleased.end() && it->second;
    }

    // 帧末调用：把当前按下状态保存为"上一帧状态"，清空边沿标记
    // VulkanApp::run 在 m_input.clearDeltas() 后调用 mapper.endFrame()
    void endFrame()
    {
        if (!m_state) return;
        // 计算下一帧的 justPressed / justReleased
        for (const auto& [action, keys] : m_actionToKeys)
        {
            bool now = false;
            for (int k : keys)
                if (m_state->key(k)) { now = true; break; }
            bool prev = m_prevAction.count(action) ? m_prevAction[action] : false;
            m_justPressed[action]  = now && !prev;
            m_justReleased[action] = !now && prev;
            m_prevAction[action]   = now;
        }
    }

    // 注入 InputState（每帧同一份，所有 mapper 共享）
    void setInputState(const InputState* s) { m_state = s; }

    // 列出所有已注册动作名（调试/UI 显示用）
    std::vector<std::string> listActions() const
    {
        std::vector<std::string> v;
        v.reserve(m_actionToKeys.size());
        for (const auto& [n, _] : m_actionToKeys) v.push_back(n);
        return v;
    }

    // 获取某动作的绑定键列表（用于 UI 显示"jump = SPACE"）
    const std::vector<int>* keysFor(const std::string& action) const
    {
        auto it = m_actionToKeys.find(action);
        return it != m_actionToKeys.end() ? &it->second : nullptr;
    }

private:
    using MapType = std::unordered_map<int, std::vector<std::string>>;
    static std::vector<std::string>* findOrNull(MapType& m, int k)
    {
        auto it = m.find(k);
        return it != m.end() ? &it->second : nullptr;
    }

    const InputState* m_state = nullptr;
    std::unordered_map<std::string, std::vector<int>> m_actionToKeys;
    MapType                                            m_keyToActions;
    std::unordered_map<std::string, bool>             m_prevAction;
    std::unordered_map<std::string, bool>             m_justPressed;
    std::unordered_map<std::string, bool>             m_justReleased;
};

} // namespace ecs

