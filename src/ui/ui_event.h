/**
 * UiEvent —— 事件类型 + 交互接口
 *
 * 通过多继承（mix-in）实现多种触发事件：
 *   IClickable  —— 鼠标点击
 *   IHoverable  —— 鼠标悬停进入/离开
 *   IDraggable  —— 鼠标拖拽
 *   ITextInput  —— 文本输入（文本框）
 *
 * UiElement 基类不直接继承这些接口；具体控件按需多继承，
 * 例如 UiButton : UiElement, IClickable, IHoverable，
 * 构成 "多种继承关系"。
 */
#pragma once

#include <string>
#include <functional>
#include <utility>

// ---- 事件载荷 ----
struct UiMouseEvent
{
    float x, y;     // 像素坐标（窗口左上为原点）
    int   button;   // 0=左键, 1=右键, 2=中键
    bool  pressed;  // true=按下, false=释放
};

struct UiKeyEvent
{
    int   key;      // GLFW key 常量
    int   scancode;
    int   action;   // GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT
    int   mods;
    char  character;// 文本字符（GLFW_CHAR 回调）
};

// ---- 事件回调类型 ----
using ClickHandler  = std::function<void()>;
using HoverHandler  = std::function<void()>;
using DragHandler    = std::function<void(float dx, float dy)>;
using TextInputHandler = std::function<void(const std::string&)>;

// ============================================================================
// 交互接口（mix-in）
// ============================================================================

class IClickable
{
public:
    virtual ~IClickable() = default;
    virtual void onClick() {}
    void setClickHandler(ClickHandler h) { m_onClick = std::move(h); }
    ClickHandler m_onClick;
};

class IHoverable
{
public:
    virtual ~IHoverable() = default;
    virtual void onHoverEnter() {}
    virtual void onHoverLeave() {}
    void setHoverEnterHandler(HoverHandler h) { m_onEnter = std::move(h); }
    void setHoverLeaveHandler(HoverHandler h) { m_onLeave = std::move(h); }
    HoverHandler m_onEnter;
    HoverHandler m_onLeave;
};

class IDraggable
{
public:
    virtual ~IDraggable() = default;
    virtual void onDrag(float /*dx*/, float /*dy*/) {}
    void setDragHandler(DragHandler h) { m_onDrag = std::move(h); }
    DragHandler m_onDrag;
    // 拖拽内部状态
    bool m_dragging = false;
    float m_dragLastX = 0.0f;
    float m_dragLastY = 0.0f;
};

class ITextInput
{
public:
    virtual ~ITextInput() = default;
    virtual void onTextInput(const std::string& /*text*/) {}
    void setInputHandler(TextInputHandler h) { m_onInput = std::move(h); }
    TextInputHandler m_onInput;
};
