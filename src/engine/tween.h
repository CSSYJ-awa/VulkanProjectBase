/**
 * tween.h —— 缓动动画系统（v1.0.2）
 *
 * 功能：
 *   - 对任意 float 标量目标做补间（to / toVec3 / toColor4）
 *   - 内置 8 种缓动曲线（线性/二次进出/弹性/回弹等）
 *   - 支持延时启动（delay）与循环（repeat）
 *   - 完成回调（onComplete），自动回收
 *
 * 用法：
 *   tween::system().to(&angle, 3.14f, 2.0f, tween::Ease::InOutQuad);
 *   tween::system().toVec3(&pos, {10, 0, 0}, 1.5f);
 *   tween::system().toColor4(&color[0], 1, 0, 0, 1, 0.8f);
 *
 * 时间驱动：VulkanApp 主循环调用 tween::system().update(m_time.dt)（受全局时间缩放影响）。
 * 非侵入式：直接操作调用方持有的变量指针，无需继承或注册。
 */
#pragma once

#include <cstdint>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace tween {

using TweenId = uint64_t;

// ============================================================================
// 缓动曲线
// ============================================================================
enum class Ease
{
    Linear,      // 匀速
    InQuad,      // 加速
    OutQuad,     // 减速
    InOutQuad,   // 先加速后减速
    InCubic,
    OutCubic,
    InOutCubic,
    OutBack,     // 回弹（略超目标再回落）
    OutElastic,  // 弹性振荡
};

inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

// 缓动函数：t ∈ [0,1] → 变换后进度（同样 ∈ [0,1] 附近）
inline float applyEase(Ease e, float t)
{
    t = clamp01(t);
    switch (e)
    {
    case Ease::Linear:     return t;
    case Ease::InQuad:     return t * t;
    case Ease::OutQuad:    return t * (2.0f - t);
    case Ease::InOutQuad:  return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    case Ease::InCubic:    return t * t * t;
    case Ease::OutCubic:   { float u = t - 1.0f; return u * u * u + 1.0f; }
    case Ease::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
    case Ease::OutBack:    { const float c1 = 1.70158f, c3 = c1 + 1.0f; float u = t - 1.0f; return 1.0f + c3 * u * u * u + c1 * u * u; }
    case Ease::OutElastic:
    {
        const float c4 = 2.0943951f; // 2π/3
        if (t == 0.0f || t == 1.0f) return t;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
    }
    return t;
}

// ============================================================================
// TweenSystem —— 补间动画管理器
// ============================================================================
class TweenSystem
{
public:
    TweenSystem() = default;
    ~TweenSystem() = default;

    TweenSystem(const TweenSystem&) = delete;
    TweenSystem& operator=(const TweenSystem&) = delete;

    // 标量补间：把 *target 从当前值缓动到 to
    TweenId to(float* target, float to, float duration,
               Ease e = Ease::Linear, float delay = 0.0f,
               int repeat = 0, std::function<void()> onComplete = nullptr)
    {
        return add(target, duration, e, delay, repeat, std::move(onComplete), to);
    }

    // 向量补间（分量独立，各分量同曲线同延迟）
    TweenId toVec3(glm::vec3* target, const glm::vec3& to, float duration,
                   Ease e = Ease::Linear, float delay = 0.0f)
    {
        add(&target->x, duration, e, delay, 0, nullptr, to.x);
        add(&target->y, duration, e, delay, 0, nullptr, to.y);
        add(&target->z, duration, e, delay, 0, nullptr, to.z);
        return m_nextId;   // 返回最后一个分量的 id（cancel 时需逐个 cancel 或 clear）
    }

    // 颜色补间（RGBA 四分量）
    TweenId toColor4(float* rgba, float r, float g, float b, float a,
                     float duration, Ease e = Ease::Linear, float delay = 0.0f)
    {
        const float to[4] = { r, g, b, a };
        TweenId last = 0;
        for (int i = 0; i < 4; ++i)
            last = add(&rgba[i], duration, e, delay, 0, nullptr, to[i]);
        return last;
    }

    void cancel(TweenId id)
    {
        for (auto it = m_items.begin(); it != m_items.end(); ++it)
            if (it->id == id) { m_items.erase(it); return; }
    }

    // 推进全部补间（传入已缩放 dt）
    void update(float dt)
    {
        for (size_t i = 0; i < m_items.size();)
        {
            Item& it = m_items[i];
            if (it.delay > 0.0f)
            {
                it.delay -= dt;
                ++i;
                continue;
            }
            it.elapsed += dt;
            float dur = it.duration > 0.0f ? it.duration : 1.0f;
            float t = applyEase(it.ease, it.elapsed / dur);
            if (it.target) *it.target = it.from + (it.to - it.from) * t;

            if (it.elapsed >= it.duration)
            {
                if (it.repeat != 0)
                {
                    // 循环：重置进度
                    it.elapsed = 0.0f;
                    if (it.repeat > 0) --it.repeat;
                    it.from = it.to;   // 从当前值继续
                    ++i;
                }
                else
                {
                    if (it.target) *it.target = it.to;   // 确保精确落在目标值
                    auto cb = std::move(it.onComplete);
                    m_items.erase(m_items.begin() + static_cast<long>(i));
                    if (cb) cb();
                }
            }
            else { ++i; }
        }
    }

    void clear() { m_items.clear(); m_nextId = 0; }
    size_t count() const { return m_items.size(); }

private:
    struct Item
    {
        TweenId id = 0;
        float*  target = nullptr;
        float   from = 0.0f, to = 0.0f;
        float   elapsed = 0.0f, duration = 1.0f;
        float   delay = 0.0f;
        int     repeat = 0;
        Ease    ease = Ease::Linear;
        std::function<void()> onComplete;
    };

    TweenId add(float* target, float duration, Ease e, float delay, int repeat,
                std::function<void()> onComplete, float to)
    {
        TweenId id = ++m_nextId;
        m_items.push_back({ id, target,
                            target ? *target : 0.0f, to,
                            0.0f, duration > 0.0f ? duration : 1.0f,
                            delay, repeat, e, std::move(onComplete) });
        return id;
    }

    std::vector<Item> m_items;
    TweenId m_nextId = 0;
};

// 全局补间单例
inline TweenSystem& system()
{
    static TweenSystem instance;
    return instance;
}

} // namespace tween
