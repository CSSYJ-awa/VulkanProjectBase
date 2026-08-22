/**
 * UiWidgets 实现
 */
#include "ui_widgets.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <algorithm>

// 全局唯一焦点文本框
UiTextBox* UiTextBox::s_focused = nullptr;

// ============================================================================
// UiElement 基类实现
// ============================================================================

void UiElement::addChild(std::unique_ptr<UiElement> child)
{
    child->m_parent = this;
    // JSON 中子节点坐标是相对于父节点的，转换为绝对坐标（递归平移整个子树）
    child->translate(m_x, m_y);
    m_children.push_back(std::move(child));
}

bool UiElement::contains(float px, float py) const
{
    return px >= m_x && px <= m_x + m_width &&
           py >= m_y && py <= m_y + m_height;
}

void UiElement::translate(float dx, float dy)
{
    m_x += dx;
    m_y += dy;
    markDirty();
    for (auto& c : m_children)
        c->translate(dx, dy);
}

UiElement* UiElement::findByName(const std::string& name)
{
    if (m_name == name) return this;
    for (auto& c : m_children)
        if (auto* r = c->findByName(name)) return r;
    return nullptr;
}

void UiElement::draw(const UiRenderContext& ctx)
{
    if (!m_visible) return;

    drawSelf(ctx);

    for (auto& c : m_children)
        c->draw(ctx);
}

void UiElement::drawSelf(const UiRenderContext& ctx)
{
    // 默认绘制背景矩形（alpha > 0 才可见）
    if (m_color[3] <= 0.0f) return;

    // 把像素坐标 + 尺寸转换为 NDC
    float nx0, ny0, nx1, ny1;
    pixelToNdc(m_x, m_y, ctx.extent.width, ctx.extent.height, nx0, ny0);
    pixelToNdc(m_x + m_width, m_y + m_height, ctx.extent.width, ctx.extent.height, nx1, ny1);
    float cx = (nx0 + nx1) * 0.5f;
    float cy = (ny0 + ny1) * 0.5f;
    float w  = nx1 - nx0;
    float h  = ny1 - ny0; // Vulkan NDC Y 向下，ny1(下) > ny0(上)

    // 仅在脏标记、首次上传、或渲染区域变化时重建顶点
    bool extentChanged = (m_lastExtentW != ctx.extent.width || m_lastExtentH != ctx.extent.height);
    if (m_dirty || !m_bgUploaded || extentChanged)
    {
        m_bg.setBounds(cx, cy, w, h, m_color[0], m_color[1], m_color[2]);
        m_bg.setColor(m_color[0], m_color[1], m_color[2], m_color[3]);
        m_bg.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_bgUploaded = true;
        m_dirty = false;
        m_lastExtentW = ctx.extent.width;
        m_lastExtentH = ctx.extent.height;
    }
    else
    {
        // 仅更新颜色（push constant）
        m_bg.setColor(m_color[0], m_color[1], m_color[2], m_color[3]);
    }
    m_bg.drawVBOOnly(ctx.commandBuffer, ctx.pipelineLayout);
}

bool UiElement::handleMouseEvent(const UiMouseEvent& e)
{
    if (!m_visible) return false;

    // 优先分发给子节点（后绘制的在上）
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        if ((*it)->handleMouseEvent(e)) return true;

    return handleMouseEventSelf(e);
}

bool UiElement::handleKeyEvent(const UiKeyEvent& e)
{
    if (!m_visible) return false;
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        if ((*it)->handleKeyEvent(e)) return true;
    return handleKeyEventSelf(e);
}

// ============================================================================
// UiText 实现
// ============================================================================

UiText::UiText()
{
    // 文字元素默认无背景
    m_color[3] = 0.0f;
    m_text = "Text";
}

void UiText::setText(const std::string& t)
{
    m_text = t;
    markDirty();
}

void UiText::drawSelf(const UiRenderContext& ctx)
{
    // 先绘制背景矩形（alpha>0 时基类会渲染，如 UiTextBox 的深色背景）
    UiElement::drawSelf(ctx);

    // 此处绘制文字标签
    float labelX = x();
    float labelY = y();
    bool extentChanged = (m_lastLabelExtentW != ctx.extent.width ||
                          m_lastLabelExtentH != ctx.extent.height);
    bool posChanged = (m_lastLabelX != labelX || m_lastLabelY != labelY);

    if (m_dirty || !m_labelUploaded)
    {
        // 首次上传或内容变化：更新文字、字号、位置，然后上传
        m_label.setText(m_text);
        m_label.setPixelSize(m_fontPx, ctx.extent.width);
        m_label.setPixelPosition(labelX, labelY, ctx.extent.width, ctx.extent.height);
        m_label.setColor(m_textColor[0], m_textColor[1], m_textColor[2], m_textColor[3]);
        m_label.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_labelUploaded = true;
        m_dirty = false;
        m_lastLabelX = labelX;
        m_lastLabelY = labelY;
        m_lastLabelExtentW = ctx.extent.width;
        m_lastLabelExtentH = ctx.extent.height;
    }
    else if (extentChanged || posChanged)
    {
        // 窗口尺寸或位置变化：重新计算 NDC 坐标并上传
        if (extentChanged)
            m_label.setPixelSize(m_fontPx, ctx.extent.width);
        m_label.setPixelPosition(labelX, labelY, ctx.extent.width, ctx.extent.height);
        m_label.setColor(m_textColor[0], m_textColor[1], m_textColor[2], m_textColor[3]);
        m_label.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_lastLabelX = labelX;
        m_lastLabelY = labelY;
        m_lastLabelExtentW = ctx.extent.width;
        m_lastLabelExtentH = ctx.extent.height;
    }
    m_label.drawVBOOnly(ctx.commandBuffer, ctx.pipelineLayout);
}

// ============================================================================
// UiButton 实现
// ============================================================================

UiButton::UiButton()
{
    // 按钮默认背景色
    m_color[0] = m_normalColor[0];
    m_color[1] = m_normalColor[1];
    m_color[2] = m_normalColor[2];
    m_color[3] = 1.0f;
}

void UiButton::setText(const std::string& t)
{
    m_text = t;
    markDirty();
}

void UiButton::onClick()
{
    if (m_onClick) m_onClick();
}

void UiButton::onHoverEnter()
{
    m_hovered = true;
    if (!m_pressed)
    {
        m_color[0] = m_hoverColor[0];
        m_color[1] = m_hoverColor[1];
        m_color[2] = m_hoverColor[2];
        markDirty();
    }
    if (m_onEnter) m_onEnter();
}

void UiButton::onHoverLeave()
{
    m_hovered = false;
    if (!m_pressed)
    {
        m_color[0] = m_normalColor[0];
        m_color[1] = m_normalColor[1];
        m_color[2] = m_normalColor[2];
        markDirty();
    }
    if (m_onLeave) m_onLeave();
}

bool UiButton::handleMouseEventSelf(const UiMouseEvent& e)
{
    // 释放事件必须无条件复位按下状态：即使用户在按钮上按下后拖出再释放，
    // 也不能让按钮卡在 pressed 状态（否则颜色无法恢复、点击回调被吞掉）
    if (e.button == 0 && !e.pressed && m_pressed)
    {
        m_pressed = false;
        // 悬停状态决定回到的颜色
        m_color[0] = m_hovered ? m_hoverColor[0] : m_normalColor[0];
        m_color[1] = m_hovered ? m_hoverColor[1] : m_normalColor[1];
        m_color[2] = m_hovered ? m_hoverColor[2] : m_normalColor[2];
        markDirty();
        // 仅在按钮内释放才触发点击
        if (contains(e.x, e.y)) onClick();
        return true;
    }
    if (!contains(e.x, e.y)) return false;

    if (e.button == 0 && e.pressed)
    {
        m_pressed = true;
        m_color[0] = m_pressedColor[0];
        m_color[1] = m_pressedColor[1];
        m_color[2] = m_pressedColor[2];
        markDirty();
        return true;
    }
    return false;
}

void UiButton::drawSelf(const UiRenderContext& ctx)
{
    // 绘制背景
    UiElement::drawSelf(ctx);

    // 绘制文字标签（文字在按钮内部居中，简化为左对齐 + padding）
    float labelX = x() + 8;
    float labelY = y() + 6;
    bool extentChanged = (m_lastLabelExtentW != ctx.extent.width ||
                          m_lastLabelExtentH != ctx.extent.height);
    bool posChanged = (m_lastLabelX != labelX || m_lastLabelY != labelY);

    if (m_dirty || !m_labelUploaded)
    {
        m_label.setText(m_text);
        m_label.setPixelSize(m_fontPx, ctx.extent.width);
        m_label.setPixelPosition(labelX, labelY, ctx.extent.width, ctx.extent.height);
        m_label.setColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_label.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_labelUploaded = true;
        m_lastLabelX = labelX;
        m_lastLabelY = labelY;
        m_lastLabelExtentW = ctx.extent.width;
        m_lastLabelExtentH = ctx.extent.height;
    }
    else if (extentChanged || posChanged)
    {
        if (extentChanged)
            m_label.setPixelSize(m_fontPx, ctx.extent.width);
        m_label.setPixelPosition(labelX, labelY, ctx.extent.width, ctx.extent.height);
        m_label.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_lastLabelX = labelX;
        m_lastLabelY = labelY;
        m_lastLabelExtentW = ctx.extent.width;
        m_lastLabelExtentH = ctx.extent.height;
    }
    m_label.drawVBOOnly(ctx.commandBuffer, ctx.pipelineLayout);
}

// ============================================================================
// UiTextBox 实现
// ============================================================================

UiTextBox::UiTextBox()
{
    m_color[0] = 0.15f;
    m_color[1] = 0.15f;
    m_color[2] = 0.2f;
    m_color[3] = 1.0f;
}

UiTextBox::~UiTextBox()
{
    // 如果当前销毁的是焦点文本框，清空全局指针
    if (s_focused == this)
        s_focused = nullptr;
}

void UiTextBox::clearAllFocus()
{
    if (s_focused)
    {
        s_focused->m_focused = false;
        s_focused = nullptr;
    }
}

void UiTextBox::onTextInput(const std::string& text)
{
    m_text += text;
    markDirty();
    if (m_onInput) m_onInput(m_text);
}

bool UiTextBox::handleMouseEventSelf(const UiMouseEvent& e)
{
    if (e.button == 0 && e.pressed)
    {
        if (contains(e.x, e.y))
        {
            // 点击本文本框：切换全局焦点
            if (s_focused && s_focused != this)
                s_focused->m_focused = false;
            m_focused = true;
            s_focused = this;
            return true;
        }
        // 点击不在本文本框区域：不消费，交给 UiManager 统一清空焦点
    }
    return false;
}

bool UiTextBox::handleKeyEventSelf(const UiKeyEvent& e)
{
    if (!m_focused) return false;

    // 1) 文本字符输入（来自 onChar 回调，UTF-8 编码的完整字符串）
    if (!e.character.empty())
    {
        onTextInput(e.character);
        return true;
    }

    // 2) 特殊按键：仅响应 PRESS/REPEAT（忽略 RELEASE 避免重复触发）
    if (e.action == GLFW_RELEASE) return false;

    switch (e.key)
    {
    case GLFW_KEY_BACKSPACE:
        if (!m_text.empty())
        {
            // 简单删除最后一个 UTF-8 字节（ASCII 场景够用，可后续扩展为 UTF-8 安全）
            size_t pos = m_text.size() - 1;
            while (pos > 0 && (m_text[pos] & 0xC0) == 0x80)
                --pos; // UTF-8: 回溯到起始字节
            m_text.erase(pos);
            markDirty();
            if (m_onInput) m_onInput(m_text);
        }
        return true;

    case GLFW_KEY_DELETE:
        // 单行文框等同退格（后续若支持光标可改为删光标后方）
        if (!m_text.empty())
        {
            size_t pos = m_text.size() - 1;
            while (pos > 0 && (m_text[pos] & 0xC0) == 0x80)
                --pos;
            m_text.erase(pos);
            markDirty();
            if (m_onInput) m_onInput(m_text);
        }
        return true;

    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        // 回车可选：提交事件或追加换行。此处不追加（单行文框）。
        return true;

    case GLFW_KEY_ESCAPE:
        // ESC 失焦
        m_focused = false;
        if (s_focused == this)
            s_focused = nullptr;
        return true;

    default:
        break;
    }
    return false;
}

// ============================================================================
// UiPanel 实现
// ============================================================================

UiPanel::UiPanel()
{
    m_color[0] = 0.2f;
    m_color[1] = 0.2f;
    m_color[2] = 0.28f;
    m_color[3] = 0.85f;
}

void UiPanel::onDrag(float dx, float dy)
{
    // 移动面板自身及其所有子节点（递归）
    translate(dx, dy);
    if (m_onDrag) m_onDrag(dx, dy);
}

bool UiPanel::handleMouseEventSelf(const UiMouseEvent& e)
{
    if (!contains(e.x, e.y)) return false;

    // v1.2：拖拽关闭时不响应按下（仍允许事件冒泡以让子节点接收）
    if (!m_draggableEnabled) return false;

    if (e.button == 0)
    {
        if (e.pressed)
        {
            m_dragging = true;
            m_dragLastX = e.x;
            m_dragLastY = e.y;
            return true;
        }
        else if (m_dragging)
        {
            m_dragging = false;
            return true;
        }
    }
    return false;
}

// 注意：UiPanel 的拖拽需要每帧更新位置——这里通过 manager 调用 update(dt) 时
// 检查 m_dragging 并根据当前鼠标位置移动。简化：在 manager 中处理。

// ============================================================================
// UiCheckbox 实现
// ============================================================================

UiCheckbox::UiCheckbox()
{
    // 方框背景色
    m_color[0] = m_boxColor[0];
    m_color[1] = m_boxColor[1];
    m_color[2] = m_boxColor[2];
    m_color[3] = m_boxColor[3];
}

void UiCheckbox::setChecked(bool v)
{
    if (m_checked == v) return;
    m_checked = v;
    markDirty();
    if (m_onChecked) m_onChecked(m_checked);
}

void UiCheckbox::setBoxColor(float r, float g, float b, float a)
{
    m_boxColor[0] = r; m_boxColor[1] = g;
    m_boxColor[2] = b; m_boxColor[3] = a;
    m_color[0] = r; m_color[1] = g; m_color[2] = b; m_color[3] = a;
    markDirty();
}

void UiCheckbox::setCheckColor(float r, float g, float b, float a)
{
    m_checkColor[0] = r; m_checkColor[1] = g;
    m_checkColor[2] = b; m_checkColor[3] = a;
    markDirty();
}

void UiCheckbox::onClick()
{
    setChecked(!m_checked);
}

bool UiCheckbox::handleMouseEventSelf(const UiMouseEvent& e)
{
    if (!contains(e.x, e.y)) return false;

    if (e.button == 0 && e.pressed)
    {
        m_pressed = true;
        return true;
    }
    if (e.button == 0 && !e.pressed && m_pressed)
    {
        m_pressed = false;
        onClick();
        return true;
    }
    return false;
}

void UiCheckbox::updateCheckMark(const UiRenderContext& ctx)
{
    // 对勾为内缩约 25% 的实心方块（居中）
    float inset = std::min(width(), height()) * 0.25f;
    float nx0, ny0, nx1, ny1;
    pixelToNdc(x() + inset, y() + inset, ctx.extent.width, ctx.extent.height, nx0, ny0);
    pixelToNdc(x() + width() - inset, y() + height() - inset,
               ctx.extent.width, ctx.extent.height, nx1, ny1);
    float cx = (nx0 + nx1) * 0.5f;
    float cy = (ny0 + ny1) * 0.5f;
    float w  = nx1 - nx0;
    float h  = ny1 - ny0;

    bool extentChanged = (m_lastExtentW != ctx.extent.width ||
                          m_lastExtentH != ctx.extent.height);
    if (m_dirty || !m_markUploaded || extentChanged)
    {
        m_mark.setBounds(cx, cy, w, h, m_checkColor[0], m_checkColor[1], m_checkColor[2]);
        m_mark.setColor(m_checkColor[0], m_checkColor[1], m_checkColor[2], m_checkColor[3]);
        m_mark.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_markUploaded = true;
        m_dirty = false;
        m_lastExtentW = ctx.extent.width;
        m_lastExtentH = ctx.extent.height;
    }
    else
    {
        m_mark.setColor(m_checkColor[0], m_checkColor[1], m_checkColor[2], m_checkColor[3]);
    }
    m_mark.drawVBOOnly(ctx.commandBuffer, ctx.pipelineLayout);
}

void UiCheckbox::drawSelf(const UiRenderContext& ctx)
{
    // 方框背景
    UiElement::drawSelf(ctx);
    // 选中时绘制对勾
    if (m_checked)
        updateCheckMark(ctx);
}

// ============================================================================
// UiSlider 实现
// ============================================================================

UiSlider::UiSlider()
{
    // 轨道背景色
    m_color[0] = m_trackColor[0];
    m_color[1] = m_trackColor[1];
    m_color[2] = m_trackColor[2];
    m_color[3] = m_trackColor[3];
}

void UiSlider::setValue(float v)
{
    float old = m_value;
    m_value = v;
    clampValue();
    if (m_value != old)
    {
        markDirty();
        if (m_onValueChanged) m_onValueChanged(m_value);
    }
}

void UiSlider::clampValue()
{
    if (m_value < m_min) m_value = m_min;
    if (m_value > m_max) m_value = m_max;
}

void UiSlider::setTrackColor(float r, float g, float b, float a)
{
    m_trackColor[0] = r; m_trackColor[1] = g;
    m_trackColor[2] = b; m_trackColor[3] = a;
    m_color[0] = r; m_color[1] = g; m_color[2] = b; m_color[3] = a;
    markDirty();
}

void UiSlider::setKnobColor(float r, float g, float b, float a)
{
    m_knobColor[0] = r; m_knobColor[1] = g;
    m_knobColor[2] = b; m_knobColor[3] = a;
    markDirty();
}

void UiSlider::setValueFromPixelX(float px)
{
    float span = (m_max - m_min);
    if (span <= 0.0f) { setValue(m_min); return; }
    float t = (px - x()) / width();
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    setValue(m_min + t * span);
}

bool UiSlider::handleMouseEventSelf(const UiMouseEvent& e)
{
    if (!contains(e.x, e.y)) return false;

    if (e.button == 0 && e.pressed)
    {
        m_pressed = true;
        // 点击轨道任意位置：直接跳转
        setValueFromPixelX(e.x);
        return true;
    }
    if (e.button == 0 && !e.pressed && m_pressed)
    {
        m_pressed = false;
        return true;
    }
    return false;
}

void UiSlider::updateKnob(const UiRenderContext& ctx)
{
    // 游标为与轨道同高的方块，中心位于值对应位置
    float knobW = height();
    float t = (m_max > m_min) ? (m_value - m_min) / (m_max - m_min) : 0.0f;
    float cxPx = x() + t * (width() - knobW) + knobW * 0.5f;
    float cyPx = y() + height() * 0.5f;

    float nx0, ny0, nx1, ny1;
    pixelToNdc(cxPx - knobW * 0.5f, cyPx - height() * 0.5f,
               ctx.extent.width, ctx.extent.height, nx0, ny0);
    pixelToNdc(cxPx + knobW * 0.5f, cyPx + height() * 0.5f,
               ctx.extent.width, ctx.extent.height, nx1, ny1);
    float cx = (nx0 + nx1) * 0.5f;
    float cy = (ny0 + ny1) * 0.5f;
    float w  = nx1 - nx0;
    float h  = ny1 - ny0;

    bool extentChanged = (m_lastExtentW != ctx.extent.width ||
                          m_lastExtentH != ctx.extent.height);
    if (m_dirty || !m_knobUploaded || extentChanged)
    {
        m_knob.setBounds(cx, cy, w, h, m_knobColor[0], m_knobColor[1], m_knobColor[2]);
        m_knob.setColor(m_knobColor[0], m_knobColor[1], m_knobColor[2], m_knobColor[3]);
        m_knob.upload(ctx.device, ctx.physicalDevice, ctx.commandPool, ctx.queue);
        m_knobUploaded = true;
        m_dirty = false;
        m_lastExtentW = ctx.extent.width;
        m_lastExtentH = ctx.extent.height;
    }
    else
    {
        m_knob.setColor(m_knobColor[0], m_knobColor[1], m_knobColor[2], m_knobColor[3]);
    }
    m_knob.drawVBOOnly(ctx.commandBuffer, ctx.pipelineLayout);
}

void UiSlider::drawSelf(const UiRenderContext& ctx)
{
    // 轨道背景
    UiElement::drawSelf(ctx);
    // 游标
    updateKnob(ctx);
}
