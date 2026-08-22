/**
 * UiManager 实现
 */
#include "ui_manager.h"
#include "ui_loader.h"
#include "ui_widgets.h"

#include <iostream>
#include <functional>

bool UiManager::loadFromFile(const std::string& path)
{
    setRoot(UiLoader::loadFromFile(path));
    if (m_root)
        std::cout << "[UiManager] UI 配置加载成功: " << path << std::endl;
    else
        std::cout << "[UiManager] UI 配置加载失败，将使用空 UI。" << std::endl;
    return m_root != nullptr;
}

bool UiManager::loadFromFile(const std::string& path, const UiBindings& bindings)
{
    if (!loadFromFile(path)) return false;
    UiLoader::bindEvents(m_root.get(), bindings);
    return true;
}

void UiManager::bindEvents(const UiBindings& bindings)
{
    UiLoader::bindEvents(m_root.get(), bindings);
}

void UiManager::setRoot(std::unique_ptr<UiElement> root)
{
    m_root = std::move(root);
    // 树被替换/清空：旧树元素即将析构，必须清空悬空指针（use-after-free 防护）
    m_hovered = nullptr;
    m_dragTarget = nullptr;
    m_leftDown = false;
}

// 深度优先命中测试（最上层优先）
UiElement* UiManager::hitTest(UiElement* cur, double px, double py)
{
    if (!cur || !cur->visible()) return nullptr;
    // 与 UiElement::handleMouseEvent 的逆序（后添加在上层）一致
    auto& children = cur->childrenMut();
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        if (auto* r = hitTest(it->get(), px, py)) return r;
    if (cur->contains(static_cast<float>(px), static_cast<float>(py)))
        return cur;
    return nullptr;
}

// 把 Unicode 码点编码为 UTF-8 字符串（支持中文/emoji 等非 ASCII 输入）
static std::string utf8Encode(uint32_t cp)
{
    std::string s;
    if (cp < 0x80)
        s += static_cast<char>(cp);
    else if (cp < 0x800)
    {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

void UiManager::onMouseMove(double px, double py)
{
    m_mouseX = px;
    m_mouseY = py;
    if (!m_root) return;

    // 若正在拖拽，更新拖拽目标位置
    if (m_leftDown && m_dragTarget)
    {
        auto* drag = dynamic_cast<IDraggable*>(m_dragTarget);
        if (drag)
        {
            // 绝对位置回调（滑块等按位置映射值的控件）
            drag->onDragAt(static_cast<float>(m_mouseX), static_cast<float>(m_mouseY));
            // 面板沿用增量拖拽（移动自身及其子节点）
            auto* panel = dynamic_cast<UiPanel*>(m_dragTarget);
            if (panel)
            {
                float dx = static_cast<float>(m_mouseX - panel->m_dragLastX);
                float dy = static_cast<float>(m_mouseY - panel->m_dragLastY);
                panel->m_dragLastX = static_cast<float>(m_mouseX);
                panel->m_dragLastY = static_cast<float>(m_mouseY);
                panel->onDrag(dx, dy);
            }
        }
        return;
    }

    // hover 检测（最上层优先，与事件分发顺序一致）
    UiMouseEvent e{ static_cast<float>(px), static_cast<float>(py), 0, false };
    UiElement* hit = hitTest(m_root.get(), px, py);

    if (m_hovered != hit)
    {
        if (m_hovered)
        {
            auto* hb = dynamic_cast<UiButton*>(m_hovered);
            if (hb) hb->onHoverLeave();
        }
        m_hovered = hit;
        if (hit)
        {
            auto* hb = dynamic_cast<UiButton*>(hit);
            if (hb) hb->onHoverEnter();
        }
    }
}

void UiManager::onMouseButton(int button, int action, int mods)
{
    if (!m_root) return;

    bool pressed = (action == 1); // GLFW_PRESS=1
    UiMouseEvent e{ static_cast<float>(m_mouseX), static_cast<float>(m_mouseY),
                    button, pressed };

    if (button == 0)
    {
        m_leftDown = pressed;
        if (pressed)
        {
            // 找到命中元素，若是 panel 则开始拖拽
            UiElement* hit = hitTest(m_root.get(), m_mouseX, m_mouseY);
            if (hit)
            {
                // 向上找到第一个可拖拽的目标（UiPanel / UiSlider 等 IDraggable）
                UiElement* p = hit;
                while (p)
                {
                    // 面板已禁用拖拽时跳过（滑块等仍可拖）
                    auto* panel = dynamic_cast<UiPanel*>(p);
                    if (panel && !panel->draggable())
                    {
                        p = p->parent();
                        continue;
                    }
                    if (auto* drag = dynamic_cast<IDraggable*>(p))
                    {
                        m_dragTarget = p;
                        drag->m_dragging = true;
                        drag->m_dragLastX = static_cast<float>(m_mouseX);
                        drag->m_dragLastY = static_cast<float>(m_mouseY);
                        break;
                    }
                    p = p->parent();
                }
            }
            else
            {
                // 点击空白区域：清空文本框焦点
                UiTextBox::clearAllFocus();
            }
            m_root->handleMouseEvent(e);
        }
        else
        {
            m_root->handleMouseEvent(e);
            if (m_dragTarget)
            {
                if (auto* drag = dynamic_cast<IDraggable*>(m_dragTarget))
                    drag->m_dragging = false;
                m_dragTarget = nullptr;
            }
        }
    }
}

void UiManager::onKey(int key, int scancode, int action, int mods)
{
    if (!m_root) return;
    UiKeyEvent e{ key, scancode, action, mods, {} };
    m_root->handleKeyEvent(e);
}

void UiManager::onChar(unsigned int codepoint)
{
    if (!m_root) return;
    UiKeyEvent e{ 0, 0, 0, 0, utf8Encode(codepoint) };
    m_root->handleKeyEvent(e);
}

void UiManager::draw(const UiRenderContext& ctx)
{
    if (!m_root) return;
    m_root->draw(ctx);
}

void UiManager::update(float /*dt*/)
{
    if (m_root) m_root->update(0.0f);
}
