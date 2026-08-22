/**
 * UiBuilder —— 代码构建 UI 的流式 API
 *
 * 目的：让用户在 C++ 代码中以链式调用方式快速构建 UI 树，无需写 JSON。
 *
 * 示例：
 *   auto root = UiBuilder()
 *       .panel("main_panel", 880, 40, 380, 640)
 *           .color(0.12f, 0.12f, 0.18f, 0.85f)
 *           .draggable(true)
 *           .child()
 *               .button("btn_quit", "QUIT", 20, 20, 100, 30)
 *                   .onHover([]() { ... })
 *               .end()
 *               .text("lbl_title", "Settings", 20, 80, 100, 24)
 *                   .fontSmooth("Arial", 18)
 *               .end()
 *           .end()
 *       .build();
 *
 *   m_ui->setRoot(std::move(root));
 *
 * 设计：
 *   - UiBuilder 持有 m_root（unique_ptr<UiElement>）和 m_current（指向当前编辑节点）
 *   - panel/button/text/textbox 创建新元素并 push 到 m_stack（当前位置栈）
 *   - child() 进入最后添加的子节点（push 当前到栈）
 *   - end() 退出当前节点（pop 栈，回到父节点）
 *   - color/font/etc 修改 m_current 指向的元素
 *   - build() 返回 m_root 并清空状态
 *
 * 注意：调用方负责在 build() 后通过 m_ui->setRoot() 装载。
 */
#pragma once

#include "ui_widgets.h"
#include "../text/font.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

class UiBuilder
{
public:
    UiBuilder() = default;
    ~UiBuilder() = default;

    // ---- 顶层元素创建（成为新的 m_root）----
    UiBuilder& panel(const std::string& name, float x, float y, float w, float h);
    UiBuilder& button(const std::string& name, const std::string& text,
                     float x, float y, float w, float h);
    UiBuilder& text(const std::string& name, const std::string& text,
                    float x, float y, float w, float h);
    UiBuilder& textbox(const std::string& name, const std::string& placeholder,
                       float x, float y, float w, float h);
    UiBuilder& slider(const std::string& name, float x, float y, float w, float h);
    UiBuilder& checkbox(const std::string& name, float x, float y, float w, float h);

    // ---- 子元素创建（必须先在 child() 后调用）----
    UiBuilder& childPanel(const std::string& name, float x, float y, float w, float h);
    UiBuilder& childButton(const std::string& name, const std::string& text,
                           float x, float y, float w, float h);
    UiBuilder& childText(const std::string& name, const std::string& text,
                         float x, float y, float w, float h);
    UiBuilder& childTextBox(const std::string& name, const std::string& placeholder,
                            float x, float y, float w, float h);
    UiBuilder& childSlider(const std::string& name, float x, float y, float w, float h);
    UiBuilder& childCheckbox(const std::string& name, float x, float y, float w, float h);

    // ---- 通用属性修改（作用于 m_current）----
    UiBuilder& color(float r, float g, float b, float a = 1.0f);
    UiBuilder& visible(bool v);
    UiBuilder& fontSize(uint32_t px);
    // 像素字（默认）：使用内置 PixelFont
    UiBuilder& fontPixel();
    // 平滑字体：自动通过 FontRegistry::getOrCreateSmoothFont 创建/查找
    UiBuilder& fontSmooth(const std::string& systemFontName, int pixelSize);
    // 自定义字体（用户已注册过的 Font*）
    UiBuilder& font(Font* font);

    // ---- 类型特定属性 ----
    UiBuilder& draggable(bool v = true);  // 仅对 UiPanel 生效
    UiBuilder& range(float min, float max);  // 仅对 UiSlider 生效
    UiBuilder& value(float v);               // 仅对 UiSlider 生效
    UiBuilder& checked(bool v = true);       // 仅对 UiCheckbox 生效

    // 事件回调（仅对支持对应接口的元素生效，否则忽略）
    UiBuilder& onClick(std::function<void()> cb);
    UiBuilder& onHoverEnter(std::function<void()> cb);
    UiBuilder& onHoverLeave(std::function<void()> cb);
    UiBuilder& onDrag(std::function<void(float, float)> cb);
    UiBuilder& onValueChanged(std::function<void(float)> cb);  // 仅对 UiSlider 生效
    UiBuilder& onChecked(std::function<void(bool)> cb);        // 仅对 UiCheckbox 生效
    UiBuilder& onInput(std::function<void(const std::string&)> cb);  // 仅对 UiTextBox 生效

    // ---- 树导航 ----
    // 进入最后添加的子节点（push 当前到栈）
    UiBuilder& child();
    // 退出当前节点（pop 栈，回到父节点）
    UiBuilder& end();

    // ---- 完成 ----
    std::unique_ptr<UiElement> build();

    // 获取当前元素（用于直接访问，例如绑定回调时）
    UiElement* current() const { return m_current; }
    template <typename T>
    T* currentAs() const { return dynamic_cast<T*>(m_current); }

private:
    // 创建元素并加入树
    UiBuilder& appendChild(std::unique_ptr<UiElement> e, float x, float y, float w, float h);

    std::unique_ptr<UiElement> m_root;
    std::vector<UiElement*>    m_stack;   // 父节点链（用于 end() 回溯）
    UiElement*                 m_current = nullptr;  // 当前编辑节点
};
