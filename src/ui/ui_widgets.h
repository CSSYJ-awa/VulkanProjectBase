/**
 * UiWidgets —— 具体控件
 *
 * 继承关系（体现"多种继承关系"）：
 *   UiElement (single base)
 *   ├── UiText        : UiElement                              （单继承）
 *   ├── UiButton      : UiElement, IClickable, IHoverable      （多继承）
 *   ├── UiTextBox     : UiText, ITextInput                     （多继承 + 链式）
 *   └── UiPanel       : UiElement, IDraggable                   （多继承）
 *
 * UiButton 自身包含背景矩形 + 文字标签。
 * UiPanel 可被拖拽，并包含子元素列表。
 * UiText 显示一段文字。
 * UiTextBox 继承 UiText 并支持文本输入。
 */
#pragma once

#include "ui_element.h"
#include "../text/text_renderer.h"
#include <string>

// ============================================================================
// UiText —— 纯文字显示（单继承自 UiElement）
// ============================================================================
class UiText : public UiElement
{
public:
    UiText();
    void setText(const std::string& t);
    const std::string& text() const { return m_text; }
    void setFontSize(uint32_t px) { m_fontPx = px; markDirty(); }

protected:
    void drawSelf(const UiRenderContext& ctx) override;

    std::string  m_text    = "Text";
    uint32_t     m_fontPx  = 16;
    TextRenderer m_label;
    bool         m_labelUploaded = false;
    // 跟踪上次标签位置/渲染区域，避免每帧重建顶点
    float        m_lastLabelX = -1.0f;
    float        m_lastLabelY = -1.0f;
    uint32_t     m_lastLabelExtentW = 0;
    uint32_t     m_lastLabelExtentH = 0;
};

// ============================================================================
// UiButton —— 按钮（多继承：UiElement + IClickable + IHoverable）
// ============================================================================
class UiButton : public UiElement, public IClickable, public IHoverable
{
public:
    UiButton();
    void setText(const std::string& t);
    const std::string& text() const { return m_text; }
    void setFontSize(uint32_t px) { m_fontPx = px; markDirty(); }

    // 默认行为：点击时改变颜色，悬停时高亮
    void onClick() override;
    void onHoverEnter() override;
    void onHoverLeave() override;

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
    void drawSelf(const UiRenderContext& ctx) override;

    std::string  m_text    = "Button";
    uint32_t     m_fontPx  = 16;
    TextRenderer m_label;
    bool         m_labelUploaded = false;
    // 跟踪上次标签位置/渲染区域，避免每帧重建顶点
    float        m_lastLabelX = -1.0f;
    float        m_lastLabelY = -1.0f;
    uint32_t     m_lastLabelExtentW = 0;
    uint32_t     m_lastLabelExtentH = 0;
    float        m_normalColor[4]   = { 0.3f, 0.5f, 0.8f, 1.0f };
    float        m_hoverColor[4]    = { 0.4f, 0.6f, 0.9f, 1.0f };
    float        m_pressedColor[4]  = { 0.2f, 0.4f, 0.7f, 1.0f };
    bool         m_pressed = false;
    bool         m_hovered = false;
};

// ============================================================================
// UiTextBox —— 文本框（多继承：UiText + ITextInput）
// ============================================================================
class UiTextBox : public UiText, public ITextInput
{
public:
    UiTextBox();
    ~UiTextBox() override;
    // 按键输入追加到文本
    void onTextInput(const std::string& text) override;
    // 清空所有已获得焦点的文本框（点击空白区域时使用）
    static void clearAllFocus();

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
    bool handleKeyEventSelf(const UiKeyEvent& e) override;

    bool m_focused = false;
    // 跟踪当前获得焦点的文本框（全局唯一）
    static UiTextBox* s_focused;
};

// ============================================================================
// UiPanel —— 可拖动面板（多继承：UiElement + IDraggable）
// ============================================================================
class UiPanel : public UiElement, public IDraggable
{
public:
    UiPanel();
    // 拖拽时移动整个面板（及其子节点）
    void onDrag(float dx, float dy) override;

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
};
