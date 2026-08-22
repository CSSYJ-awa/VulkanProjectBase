/**
 * time_state.h —— ECS 时间状态共享结构
 *
 * TimeState 由 VulkanApp 主循环每帧填充（基于 steady_clock），
 * 提供 elapsed / dt / fps / frameCount 全局时间信息。
 *
 * 设计与 InputState 一致：
 *   - 非线程安全，单线程主循环使用
 *   - 通过指针注入系统（DebugSystem 等），保持 System 接口不变
 *   - FPS 用滑动平均：smoothFps = smoothFps * 0.9 + instantFps * 0.1
 *
 * 用法（VulkanApp::run 中）：
 *   m_time.dt = dt;
 *   m_time.elapsed += dt;
 *   m_time.frameCount++;
 *   m_time.updateFps(dt);  // 平滑 FPS
 *   // DebugSystem 读取 m_time 写入文字实体
 */
#pragma once

#include <cstdint>

namespace ecs {

struct TimeState
{
    float    elapsed    = 0.0f;    // 自启动以来累计秒数（受 timeScale 影响）
    float    dt         = 0.0f;    // 上一帧到当前帧的 delta（秒，已应用 timeScale）
    float    unscaledDt = 0.0f;    // 未缩放的原始 dt（渲染/输入等需真实时间处使用）
    float    timeScale  = 1.0f;    // 全局时间倍率（慢动作/子弹时间）
    float    fps        = 0.0f;    // 平滑后的 FPS
    uint64_t frameCount = 0;       // 累计帧数
    float    fpsAccum   = 0.0f;    // FPS 滑动平均内部状态
    float    fpsTimer   = 0.0f;    // 每秒采样一次（避免逐帧抖动）

    // v1.0.2 固定时间步：物理/确定性逻辑按固定步长推进（默认 1/60s）
    float    fixedDt         = 1.0f / 60.0f;  // 固定步长（秒）
    float    fixedAccum      = 0.0f;          // 固定步累积器
    uint32_t fixedStepIndex  = 0;             // 当前固定步序号（帧内 0..steps-1）
    uint32_t fixedStepsThisFrame = 0;         // 本帧应执行的固定步数
    uint32_t maxFixedStepsPerFrame = 8;       // 单帧最大步数（防螺旋死锁）

    // 设置全局时间倍率（0 冻结，>0 加速/减速）
    void setTimeScale(float s) { timeScale = (s > 0.0f) ? s : 0.0f; }

    // 在主循环中调用：根据 dt 更新 fps 与 frameCount（内部应用 timeScale）
    void tick(float delta)
    {
        unscaledDt  = delta;
        dt          = delta * timeScale;
        elapsed    += dt;
        frameCount++;

        fpsTimer  += delta;
        fpsAccum  += delta;
        if (fpsTimer >= 0.25f)
        {
            // 0.25 秒内的平均 FPS
            fps = (fpsAccum > 0.0f) ? (1.0f / (fpsAccum / 0.25f)) : 0.0f;
            fpsTimer = 0.0f;
            fpsAccum = 0.0f;
        }
    }

    // v1.0.2：固定时间步消费。在 tick() 之后、逻辑更新之前调用，
    // 返回本帧应执行的固定步数。逻辑系统可使用 fixedDt 推进确定性模拟；
    // 渲染仍使用 dt 插值，保证不同帧率下物理一致。
    uint32_t consumeFixedSteps()
    {
        fixedAccum += dt;
        fixedStepsThisFrame = 0;
        while (fixedAccum >= fixedDt && fixedStepsThisFrame < maxFixedStepsPerFrame)
        {
            fixedAccum -= fixedDt;
            ++fixedStepsThisFrame;
        }
        // 帧率过低时丢弃残余累积，避免"死亡螺旋"（一帧内无限步）
        if (fixedStepsThisFrame >= maxFixedStepsPerFrame) fixedAccum = 0.0f;
        fixedStepIndex = 0;
        return fixedStepsThisFrame;
    }

    // 推进到下一个固定步（配合 consumeFixedSteps 使用）
    void nextFixedStep() { ++fixedStepIndex; }

    // 重置（场景切换）
    void reset()
    {
        elapsed = dt = fps = 0.0f;
        frameCount = 0;
        fpsAccum = fpsTimer = 0.0f;
        timeScale = 1.0f;
        fixedAccum = 0.0f;
        fixedStepsThisFrame = 0;
        fixedStepIndex = 0;
    }
};

} // namespace ecs
