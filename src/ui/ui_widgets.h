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
    // v1.2：设置文字字体（nullptr = 默认 PixelFont）
    void setFont(Font* font) { m_label.setFont(font); markDirty(); }
    // v1.7：设置文字颜色（与背景色 m_color 分离，默认白色）
    void setTextColor(float r, float g, float b, float a = 1.0f)
    {
        m_textColor[0] = r; m_textColor[1] = g;
        m_textColor[2] = b; m_textColor[3] = a;
        markDirty();
    }
    const float* textColor() const { return m_textColor; }

protected:
    void drawSelf(const UiRenderContext& ctx) override;

    std::string  m_text    = "Text";
    uint32_t     m_fontPx  = 16;
    TextRenderer m_label;
    bool         m_labelUploaded = false;
    // 文字颜色（独立于背景色；TextRenderer 仅支持单色，drawSelf 用之）
    float        m_textColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
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
    // v1.2：设置按钮文字字体（nullptr = 默认 PixelFont）
    void setFont(Font* font) { m_label.setFont(font); markDirty(); }

    // v1.2：颜色访问器（代码构建 UI 常用）
    // 若按钮当前正处对应状态，会立即更新 m_color 让下次绘制反映新颜色
    void setNormalColor(float r, float g, float b, float a = 1.0f)
    {
        m_normalColor[0]=r; m_normalColor[1]=g; m_normalColor[2]=b; m_normalColor[3]=a;
        if (!m_hovered && !m_pressed) { m_color[0]=r; m_color[1]=g; m_color[2]=b; m_color[3]=a; }
        markDirty();
    }
    void setHoverColor(float r, float g, float b, float a = 1.0f)
    {
        m_hoverColor[0]=r; m_hoverColor[1]=g; m_hoverColor[2]=b; m_hoverColor[3]=a;
        if (m_hovered && !m_pressed) { m_color[0]=r; m_color[1]=g; m_color[2]=b; m_color[3]=a; }
        markDirty();
    }
    void setPressedColor(float r, float g, float b, float a = 1.0f)
    {
        m_pressedColor[0]=r; m_pressedColor[1]=g; m_pressedColor[2]=b; m_pressedColor[3]=a;
        if (m_pressed) { m_color[0]=r; m_color[1]=g; m_color[2]=b; m_color[3]=a; }
        markDirty();
    }

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

    // v1.2：拖拽开关（默认 true）。设为 false 时面板不响应拖拽输入
    void setDraggable(bool v) { m_draggableEnabled = v; }
    bool draggable() const { return m_draggableEnabled; }

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;

private:
    bool m_draggableEnabled = true;
};

// ============================================================================
// UiCheckbox —— 复选开关（多继承：UiElement + IClickable）
//
// 点击切换选中/未选中，选中时在方框内绘制对勾（实心内框）。
// 通过 setCheckedHandler 绑定回调，选中状态变化时触发。
// ============================================================================
class UiCheckbox : public UiElement, public IClickable
{
public:
    UiCheckbox();

    void setChecked(bool v);
    bool checked() const { return m_checked; }

    // 方框背景色（未选中）
    void setBoxColor(float r, float g, float b, float a = 1.0f);
    // 对勾颜色（选中时）
    void setCheckColor(float r, float g, float b, float a = 1.0f);

    using CheckedHandler = std::function<void(bool)>;
    void setCheckedHandler(CheckedHandler h) { m_onChecked = std::move(h); }

    // 点击切换状态并触发回调
    void onClick() override;

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
    void drawSelf(const UiRenderContext& ctx) override;

private:
    void updateCheckMark(const UiRenderContext& ctx);
    bool m_checked = false;
    bool m_pressed = false;
    // 方框背景 / 对勾颜色
    float m_boxColor[4]  = { 0.3f, 0.3f, 0.35f, 1.0f };
    float m_checkColor[4] = { 0.4f, 0.9f, 0.5f, 1.0f };
    // 对勾图形（小一号的内框）
    Rectangle m_mark;
    bool      m_markUploaded = false;
    uint32_t  m_lastExtentW = 0;
    uint32_t  m_lastExtentH = 0;
    CheckedHandler m_onChecked;
};

// ============================================================================
// UiSlider —— 滑块（多继承：UiElement + IDraggable）
//
// 支持：点击轨道跳转 + 拖动滑块连续调节；值域 [min,max]（默认 0~1）。
// 通过 setValueChangedHandler 绑定回调，值变化时触发（含拖拽过程）。
// 绘制：轨道（背景矩形）+ 游标（轨道上的小方块）。
// ============================================================================
class UiSlider : public UiElement, public IDraggable
{
public:
    UiSlider();

    void setRange(float mn, float mx) { m_min = mn; m_max = mx; clampValue(); markDirty(); }
    void setValue(float v);
    float value() const { return m_value; }
    float min() const { return m_min; }
    float max() const { return m_max; }

    // 轨道颜色 / 游标颜色
    void setTrackColor(float r, float g, float b, float a = 1.0f);
    void setKnobColor(float r, float g, float b, float a = 1.0f);

    using ValueHandler = std::function<void(float)>;
    void setValueChangedHandler(ValueHandler h) { m_onValueChanged = std::move(h); }

    // 拖动（绝对位置）与点击跳转共用：由像素 x 计算值
    void setValueFromPixelX(float px);

    // IDraggable：绝对位置拖拽更新
    void onDragAt(float px, float py) override { (void)py; setValueFromPixelX(px); }

protected:
    bool handleMouseEventSelf(const UiMouseEvent& e) override;
    void drawSelf(const UiRenderContext& ctx) override;

private:
    void clampValue();
    void updateKnob(const UiRenderContext& ctx);

    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_value = 0.5f;
    bool  m_pressed = false;
    // 轨道 / 游标颜色
    float m_trackColor[4] = { 0.25f, 0.25f, 0.3f, 1.0f };
    float m_knobColor[4]  = { 0.5f, 0.75f, 1.0f, 1.0f };
    Rectangle m_knob;
    bool      m_knobUploaded = false;
    uint32_t  m_lastExtentW = 0;
    uint32_t  m_lastExtentH = 0;
    ValueHandler m_onValueChanged;
};
