/**
 * UiBuilder 实现
 */
#include "ui_builder.h"
#include "../engine/logger.h"

UiBuilder& UiBuilder::appendChild(std::unique_ptr<UiElement> e, float x, float y, float w, float h)
{
    UiElement* raw = e.get();
    raw->setRect(x, y, w, h);
    // 注意：addChild 内部会调用 translate(m_x, m_y) 把子节点从相对坐标转绝对坐标
    // 因此传入 (x,y) 是相对于父节点的本地坐标
    if (m_current)
    {
        m_current->addChild(std::move(e));
    }
    else
    {
        // 顶层
        m_root = std::move(e);
    }
    m_current = raw;
    return *this;
}

UiBuilder& UiBuilder::panel(const std::string& name, float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto p = std::make_unique<UiPanel>();
    p->setName(name);
    return appendChild(std::move(p), x, y, w, h);
}

UiBuilder& UiBuilder::button(const std::string& name, const std::string& text,
                              float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto b = std::make_unique<UiButton>();
    b->setName(name);
    b->setText(text);
    return appendChild(std::move(b), x, y, w, h);
}

UiBuilder& UiBuilder::text(const std::string& name, const std::string& text,
                            float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto t = std::make_unique<UiText>();
    t->setName(name);
    t->setText(text);
    return appendChild(std::move(t), x, y, w, h);
}

UiBuilder& UiBuilder::textbox(const std::string& name, const std::string& placeholder,
                               float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto tb = std::make_unique<UiTextBox>();
    tb->setName(name);
    tb->setText(placeholder);
    return appendChild(std::move(tb), x, y, w, h);
}

UiBuilder& UiBuilder::slider(const std::string& name, float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto s = std::make_unique<UiSlider>();
    s->setName(name);
    return appendChild(std::move(s), x, y, w, h);
}

UiBuilder& UiBuilder::checkbox(const std::string& name, float x, float y, float w, float h)
{
    m_stack.clear();
    m_current = nullptr;
    auto cb = std::make_unique<UiCheckbox>();
    cb->setName(name);
    return appendChild(std::move(cb), x, y, w, h);
}

UiBuilder& UiBuilder::childPanel(const std::string& name, float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childPanel 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto p = std::make_unique<UiPanel>();
    p->setName(name);
    return appendChild(std::move(p), x, y, w, h);
}

UiBuilder& UiBuilder::childButton(const std::string& name, const std::string& text,
                                   float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childButton 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto b = std::make_unique<UiButton>();
    b->setName(name);
    b->setText(text);
    return appendChild(std::move(b), x, y, w, h);
}

UiBuilder& UiBuilder::childText(const std::string& name, const std::string& text,
                                 float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childText 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto t = std::make_unique<UiText>();
    t->setName(name);
    t->setText(text);
    return appendChild(std::move(t), x, y, w, h);
}

UiBuilder& UiBuilder::childTextBox(const std::string& name, const std::string& placeholder,
                                    float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childTextBox 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto tb = std::make_unique<UiTextBox>();
    tb->setName(name);
    tb->setText(placeholder);
    return appendChild(std::move(tb), x, y, w, h);
}

UiBuilder& UiBuilder::childSlider(const std::string& name, float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childSlider 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto s = std::make_unique<UiSlider>();
    s->setName(name);
    return appendChild(std::move(s), x, y, w, h);
}

UiBuilder& UiBuilder::childCheckbox(const std::string& name, float x, float y, float w, float h)
{
    if (!m_current) { LOG_ERROR("UI", "UiBuilder", "childCheckbox 无父节点"); return *this; }
    m_stack.push_back(m_current);
    auto cb = std::make_unique<UiCheckbox>();
    cb->setName(name);
    return appendChild(std::move(cb), x, y, w, h);
}

// ---- 通用属性 ----

UiBuilder& UiBuilder::color(float r, float g, float b, float a)
{
    if (m_current) m_current->setColor(r, g, b, a);
    return *this;
}

UiBuilder& UiBuilder::visible(bool v)
{
    if (m_current) m_current->setVisible(v);
    return *this;
}

UiBuilder& UiBuilder::fontSize(uint32_t px)
{
    if (auto* t = dynamic_cast<UiText*>(m_current))   t->setFontSize(px);
    else if (auto* b = dynamic_cast<UiButton*>(m_current)) b->setFontSize(px);
    return *this;
}

UiBuilder& UiBuilder::fontPixel()
{
    if (auto* t = dynamic_cast<UiText*>(m_current))   t->setFont(nullptr);
    else if (auto* b = dynamic_cast<UiButton*>(m_current)) b->setFont(nullptr);
    return *this;
}

UiBuilder& UiBuilder::fontSmooth(const std::string& systemFontName, int pixelSize)
{
    Font* f = FontRegistry::instance().getOrCreateSmoothFont(systemFontName, pixelSize);
    if (auto* t = dynamic_cast<UiText*>(m_current))   t->setFont(f);
    else if (auto* b = dynamic_cast<UiButton*>(m_current)) b->setFont(f);
    return *this;
}

UiBuilder& UiBuilder::font(Font* font)
{
    if (auto* t = dynamic_cast<UiText*>(m_current))   t->setFont(font);
    else if (auto* b = dynamic_cast<UiButton*>(m_current)) b->setFont(font);
    return *this;
}

UiBuilder& UiBuilder::draggable(bool v)
{
    if (auto* p = dynamic_cast<UiPanel*>(m_current)) p->setDraggable(v);
    return *this;
}

UiBuilder& UiBuilder::range(float min, float max)
{
    if (auto* s = dynamic_cast<UiSlider*>(m_current)) s->setRange(min, max);
    return *this;
}

UiBuilder& UiBuilder::value(float v)
{
    if (auto* s = dynamic_cast<UiSlider*>(m_current)) s->setValue(v);
    return *this;
}

UiBuilder& UiBuilder::checked(bool v)
{
    if (auto* cb = dynamic_cast<UiCheckbox*>(m_current)) cb->setChecked(v);
    return *this;
}

UiBuilder& UiBuilder::onClick(std::function<void()> cb)
{
    if (auto* c = dynamic_cast<IClickable*>(m_current)) c->setClickHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onHoverEnter(std::function<void()> cb)
{
    if (auto* h = dynamic_cast<IHoverable*>(m_current)) h->setHoverEnterHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onHoverLeave(std::function<void()> cb)
{
    if (auto* h = dynamic_cast<IHoverable*>(m_current)) h->setHoverLeaveHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onDrag(std::function<void(float, float)> cb)
{
    if (auto* d = dynamic_cast<IDraggable*>(m_current)) d->setDragHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onValueChanged(std::function<void(float)> cb)
{
    if (auto* s = dynamic_cast<UiSlider*>(m_current)) s->setValueChangedHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onChecked(std::function<void(bool)> cb)
{
    if (auto* cbx = dynamic_cast<UiCheckbox*>(m_current)) cbx->setCheckedHandler(std::move(cb));
    return *this;
}

UiBuilder& UiBuilder::onInput(std::function<void(const std::string&)> cb)
{
    if (auto* t = dynamic_cast<ITextInput*>(m_current)) t->setInputHandler(std::move(cb));
    return *this;
}

// ---- 树导航 ----

UiBuilder& UiBuilder::child()
{
    if (!m_current) return *this;
    if (m_current->children().empty()) return *this;
    // 进入最后添加的子节点
    m_stack.push_back(m_current);
    m_current = m_current->children().back().get();
    return *this;
}

UiBuilder& UiBuilder::end()
{
    if (m_stack.empty()) return *this;
    m_current = m_stack.back();
    m_stack.pop_back();
    return *this;
}

std::unique_ptr<UiElement> UiBuilder::build()
{
    m_stack.clear();
    m_current = nullptr;
    return std::move(m_root);
}
