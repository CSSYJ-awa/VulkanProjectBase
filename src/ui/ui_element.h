/**
 * UiElement —— UI 元素基类
 *
 * 提供：树形结构（父子关系）、命中测试、绘制/事件分发框架。
 * 具体控件通过多继承 IClickable / IHoverable / IDraggable / ITextInput
 * 获得"多种触发事件"。
 *
 * 坐标系：像素（窗口左上为原点，Y 向下）。绘制时由内部 Shape
 * 转换为 NDC。
 */
#pragma once

#include "ui_event.h"
#include "../shapes/shape.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

// 绘制 UI 所需的 Vulkan 上下文（由 VulkanApp 在每帧填充）
struct UiRenderContext
{
    VkDevice         device          = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice  = VK_NULL_HANDLE;
    VkCommandPool    commandPool     = VK_NULL_HANDLE;
    VkQueue          queue           = VK_NULL_HANDLE;
    VkCommandBuffer  commandBuffer   = VK_NULL_HANDLE;
    VkPipeline       pipelineFilled  = VK_NULL_HANDLE;
    VkPipeline       pipelineLine    = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout  = VK_NULL_HANDLE;
    VkExtent2D       extent          = {0, 0};  // 渲染区域尺寸（用于像素→NDC）
    VkViewport       viewport        = {};
    VkRect2D         scissor         = {};
};

class UiElement
{
public:
    UiElement() = default;
    virtual ~UiElement() = default;

    // ---- 树结构 ----
    void addChild(std::unique_ptr<UiElement> child);
    UiElement* parent() const { return m_parent; }
    const std::vector<std::unique_ptr<UiElement>>& children() const { return m_children; }
    std::vector<std::unique_ptr<UiElement>>& childrenMut() { return m_children; }

    // ---- 属性 ----
    void setName(const std::string& n) { m_name = n; }
    const std::string& name() const { return m_name; }

    void setPosition(float x, float y) { m_x = x; m_y = y; markDirty(); }
    void setSize(float w, float h)      { m_width = w; m_height = h; markDirty(); }
    // 平移自身及所有子节点（递归）
    void translate(float dx, float dy);
    float x() const { return m_x; }
    float y() const { return m_y; }
    float width() const  { return m_width; }
    float height() const { return m_height; }

    void setVisible(bool v) { m_visible = v; }
    bool visible() const { return m_visible; }

    void setColor(float r, float g, float b, float a = 1.0f)
    { m_color[0]=r; m_color[1]=g; m_color[2]=b; m_color[3]=a; markDirty(); }
    const float* color() const { return m_color; }

    // ---- 生命周期 ----
    // 绘制自身 + 递归绘制子节点
    void draw(const UiRenderContext& ctx);
    // 事件分发：返回 true 表示该事件被消费
    bool handleMouseEvent(const UiMouseEvent& e);
    bool handleKeyEvent(const UiKeyEvent& e);
    virtual void update(float /*dt*/) {}

    // 命中测试（窗口像素坐标）
    bool contains(float px, float py) const;

    // 按名称查找子节点（深度优先）
    UiElement* findByName(const std::string& name);

protected:
    // 子类重写：绘制自身内容（不含子节点）
    virtual void drawSelf(const UiRenderContext& ctx);
    // 子类重写：处理自身事件（不含子节点）
    virtual bool handleMouseEventSelf(const UiMouseEvent& /*e*/) { return false; }
    virtual bool handleKeyEventSelf(const UiKeyEvent& /*e*/) { return false; }

    void markDirty() { m_dirty = true; }

    // 背景矩形（所有元素默认有背景，透明 alpha=0 则不可见）
    Rectangle m_bg;
    bool      m_bgUploaded = false;
    // 跟踪上次渲染区域，仅在变化时重建顶点（避免每帧 vkQueueWaitIdle）
    uint32_t  m_lastExtentW = 0;
    uint32_t  m_lastExtentH = 0;

    // 颜色（派生类构造/状态切换时直接修改）
    float m_color[4] = { 0.25f, 0.25f, 0.35f, 1.0f };
    // 是否需要重建顶点缓冲（派生类在 drawSelf 中读取并清零）
    bool  m_dirty = true;

private:
    std::string m_name;
    float m_x = 0.0f, m_y = 0.0f;
    float m_width = 0.0f, m_height = 0.0f;
    bool  m_visible = true;

    std::vector<std::unique_ptr<UiElement>> m_children;
    UiElement* m_parent = nullptr;
};
