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
    m_root = UiLoader::loadFromFile(path);
    if (m_root)
        std::cout << "[UiManager] UI 配置加载成功: " << path << std::endl;
    else
        std::cout << "[UiManager] UI 配置加载失败，将使用空 UI。" << std::endl;
    return m_root != nullptr;
}

void UiManager::setRoot(std::unique_ptr<UiElement> root)
{
    m_root = std::move(root);
}

void UiManager::onMouseMove(double px, double py)
{
    m_mouseX = px;
    m_mouseY = py;
    if (!m_root) return;

    // 若正在拖拽，更新面板位置
    if (m_leftDown && m_dragTarget)
    {
        auto* panel = dynamic_cast<UiPanel*>(m_dragTarget);
        if (panel)
        {
            float dx = static_cast<float>(m_mouseX - panel->m_dragLastX);
            float dy = static_cast<float>(m_mouseY - panel->m_dragLastY);
            panel->m_dragLastX = static_cast<float>(m_mouseX);
            panel->m_dragLastY = static_cast<float>(m_mouseY);
            panel->onDrag(dx, dy);
        }
        return;
    }

    // hover 检测
    UiMouseEvent e{ static_cast<float>(px), static_cast<float>(py), 0, false };
    // 简化：找到顶层包含鼠标的元素并触发 hoverEnter/Leave
    UiElement* hit = nullptr;
    // 深度优先查找
    std::function<UiElement*(UiElement*)> findHit =
        [&](UiElement* cur) -> UiElement* {
            if (!cur || !cur->visible()) return nullptr;
            for (auto& c : cur->childrenMut())
                if (auto* r = findHit(c.get())) return r;
            if (cur->contains(static_cast<float>(px), static_cast<float>(py)))
                return cur;
            return nullptr;
        };
    hit = findHit(m_root.get());

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
            std::function<UiElement*(UiElement*)> findHit =
                [&](UiElement* cur) -> UiElement* {
                if (!cur || !cur->visible()) return nullptr;
                for (auto& c : cur->childrenMut())
                    if (auto* r = findHit(c.get())) return r;
                if (cur->contains(static_cast<float>(m_mouseX), static_cast<float>(m_mouseY)))
                    return cur;
                return nullptr;
            };
            UiElement* hit = findHit(m_root.get());
            if (hit)
            {
                // 向上找到第一个可拖拽的 panel
                UiElement* p = hit;
                while (p)
                {
                    if (auto* panel = dynamic_cast<UiPanel*>(p))
                    {
                        m_dragTarget = p;
                        panel->m_dragging = true;
                        panel->m_dragLastX = static_cast<float>(m_mouseX);
                        panel->m_dragLastY = static_cast<float>(m_mouseY);
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
                auto* panel = dynamic_cast<UiPanel*>(m_dragTarget);
                if (panel) panel->m_dragging = false;
                m_dragTarget = nullptr;
            }
        }
    }
}

void UiManager::onKey(int key, int scancode, int action, int mods)
{
    if (!m_root) return;
    UiKeyEvent e{ key, scancode, action, mods, 0 };
    m_root->handleKeyEvent(e);
}

void UiManager::onChar(unsigned int codepoint)
{
    if (!m_root) return;
    UiKeyEvent e{ 0, 0, 0, 0, static_cast<char>(codepoint) };
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
