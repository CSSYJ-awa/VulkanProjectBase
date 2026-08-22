/**
 * Rng —— 可播种的确定性随机数工具
 *
 * 基于 std::mt19937。相同种子产生完全相同的序列，
 * 便于复现粒子效果、调试与回放。
 *
 * 用法：
 *   ecs::Rng rng(42u);            // 固定种子 → 可复现
 *   float f = rng.range(0.0f, 1.0f);
 *   glm::vec3 d = rng.onUnitSphere();
 */
#pragma once

#include <cstdint>
#include <random>

#include <glm/glm.hpp>

namespace ecs {

class Rng
{
public:
    explicit Rng(uint32_t seed = 0x9E3779B9u) { m_gen.seed(seed); }

    void seed(uint32_t s) { m_gen.seed(s); }

    // [0,1) 均匀浮点
    float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_gen); }

    // [min,max] 均匀浮点（闭区间）
    float range(float min, float max)
    {
        return std::uniform_real_distribution<float>(min, max)(m_gen);
    }

    // [min,max] 均匀整数（闭区间）
    int irange(int min, int max)
    {
        return std::uniform_int_distribution<int>(min, max)(m_gen);
    }

    // [0, 2π) 随机角度
    float angle() { return range(0.0f, 6.283185307179586f); }

    // 球面上均匀随机方向
    glm::vec3 onUnitSphere()
    {
        float z  = range(-1.0f, 1.0f);
        float az = angle();
        float r  = std::sqrt(1.0f - z * z);
        return glm::vec3(r * std::cos(az), r * std::sin(az), z);
    }

    // 全局共享实例（每次进程随机种子；需要确定性时请自行构造并 seed）
    static Rng& global()
    {
        static Rng rng(static_cast<uint32_t>(std::random_device{}()));
        return rng;
    }

private:
    std::mt19937 m_gen;
};

} // namespace ecs
