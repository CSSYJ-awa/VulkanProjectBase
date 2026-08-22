/**
 * UiLoader 实现
 */
#include "ui_loader.h"
#include "ui_widgets.h"

#include <fstream>
#include <sstream>
#include <iostream>

std::unique_ptr<UiElement> UiLoader::loadFromText(const std::string& json)
{
    auto root = parseJson(json);
    if (!root || !root->isObject())
    {
        std::cerr << "[UiLoader] JSON 解析失败。" << std::endl;
        return nullptr;
    }
    // 顶层应包含 "ui" 字段，或顶层直接是一个元素
    const JsonValue* node = root->find("ui");
    if (!node) node = root.get();
    return buildElement(*node);
}

std::unique_ptr<UiElement> UiLoader::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::cerr << "[UiLoader] 无法打开 UI 配置: " << path << std::endl;
        return nullptr;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromText(ss.str());
}

std::unique_ptr<UiElement> UiLoader::buildElement(const JsonValue& node)
{
    std::string type = node.find("type") ? node.find("type")->asString() : "panel";
    std::unique_ptr<UiElement> e;
    if (type == "button")    e = std::make_unique<UiButton>();
    else if (type == "text") e = std::make_unique<UiText>();
    else if (type == "textbox")   e = std::make_unique<UiTextBox>();
    else if (type == "slider")    e = std::make_unique<UiSlider>();
    else if (type == "checkbox")  e = std::make_unique<UiCheckbox>();
    else /* panel */        e = std::make_unique<UiPanel>();

    applyCommon(e.get(), node);

    // 类型特定属性
    if (auto* t = node.find("text"))
    {
        std::string txt = t->asString();
        if (auto* b = dynamic_cast<UiButton*>(e.get())) b->setText(txt);
        else if (auto* tx = dynamic_cast<UiText*>(e.get())) tx->setText(txt);
    }
    if (auto* fs = node.find("font_size"))
    {
        uint32_t px = static_cast<uint32_t>(fs->asNumber(16));
        if (auto* b = dynamic_cast<UiButton*>(e.get())) b->setFontSize(px);
        else if (auto* tx = dynamic_cast<UiText*>(e.get())) tx->setFontSize(px);
    }
    if (auto* drag = node.find("draggable"))
    {
        if (auto* p = dynamic_cast<UiPanel*>(e.get()))
        {
            // IDraggable 默认即可拖拽，draggable 标志控制开关
            p->setDraggable(drag->asBool(true));
        }
    }
    if (auto* s = dynamic_cast<UiSlider*>(e.get()))
    {
        // 顺序：先范围后值（值会被 clamp 到范围内）
        float mn = s->min(), mx = s->max();
        if (auto* mnj = node.find("min")) mn = static_cast<float>(mnj->asNumber(mn));
        if (auto* mxj = node.find("max")) mx = static_cast<float>(mxj->asNumber(mx));
        s->setRange(mn, mx);
        if (auto* val = node.find("value")) s->setValue(static_cast<float>(val->asNumber(s->value())));
    }
    if (auto* cb = dynamic_cast<UiCheckbox*>(e.get()))
    {
        if (auto* ck = node.find("checked")) cb->setChecked(ck->asBool(false));
    }

    // 递归构建子节点
    if (auto* kids = node.find("children"))
    {
        if (kids->isArray())
        {
            for (const auto& c : kids->arr)
                if (c) e->addChild(buildElement(*c));
        }
    }
    return e;
}

void UiLoader::applyCommon(UiElement* e, const JsonValue& node)
{
    if (auto* n = node.find("name")) e->setName(n->asString());
    if (auto* x = node.find("x")) e->setPosition(static_cast<float>(x->asNumber()), e->y());
    if (auto* y = node.find("y")) e->setPosition(e->x(), static_cast<float>(y->asNumber()));
    if (auto* w = node.find("width"))  e->setSize(static_cast<float>(w->asNumber()), e->height());
    if (auto* h = node.find("height")) e->setSize(e->width(), static_cast<float>(h->asNumber()));
    if (auto* c = node.find("color"))
    {
        if (c->isArray() && c->arr.size() >= 3)
        {
            float r = static_cast<float>(c->arr[0]->asNumber(1.0));
            float g = static_cast<float>(c->arr[1]->asNumber(1.0));
            float b = static_cast<float>(c->arr[2]->asNumber(1.0));
            float a = c->arr.size() >= 4 ? static_cast<float>(c->arr[3]->asNumber(1.0)) : 1.0f;
            e->setColor(r, g, b, a);
        }
    }
    if (auto* v = node.find("visible")) e->setVisible(v->asBool(true));
    if (auto* tc = node.find("text_color"))
    {
        if (tc->isArray() && tc->arr.size() >= 3)
        {
            float r = static_cast<float>(tc->arr[0]->asNumber(1.0));
            float g = static_cast<float>(tc->arr[1]->asNumber(1.0));
            float b = static_cast<float>(tc->arr[2]->asNumber(1.0));
            float a = tc->arr.size() >= 4 ? static_cast<float>(tc->arr[3]->asNumber(1.0)) : 1.0f;
            if (auto* txt = dynamic_cast<UiText*>(e)) txt->setTextColor(r, g, b, a);
        }
    }
    // 事件名绑定：on_click / on_hover_enter / on_hover_leave / on_drag / on_input / on_checked / on_value_changed
    static const char* kEventKeys[] = {
        "on_click", "on_hover_enter", "on_hover_leave",
        "on_drag", "on_input", "on_checked", "on_value_changed"
    };
    for (const char* key : kEventKeys)
        if (auto* ev = node.find(key))
            e->setNamedEvent(key, ev->asString());
}

void UiLoader::bindEvents(UiElement* root, const UiBindings& bindings)
{
    if (!root) return;

    // 按事件类型分派：动态转换为对应接口并绑定回调
    if (const std::string* name = root->namedEvent("on_click"))
        if (auto it = bindings.clicks.find(*name); it != bindings.clicks.end())
            if (auto* c = dynamic_cast<IClickable*>(root)) c->setClickHandler(it->second);

    if (const std::string* name = root->namedEvent("on_hover_enter"))
        if (auto it = bindings.hoverEnters.find(*name); it != bindings.hoverEnters.end())
            if (auto* h = dynamic_cast<IHoverable*>(root)) h->setHoverEnterHandler(it->second);

    if (const std::string* name = root->namedEvent("on_hover_leave"))
        if (auto it = bindings.hoverLeaves.find(*name); it != bindings.hoverLeaves.end())
            if (auto* h = dynamic_cast<IHoverable*>(root)) h->setHoverLeaveHandler(it->second);

    if (const std::string* name = root->namedEvent("on_drag"))
        if (auto it = bindings.drags.find(*name); it != bindings.drags.end())
            if (auto* d = dynamic_cast<IDraggable*>(root)) d->setDragHandler(it->second);

    if (const std::string* name = root->namedEvent("on_input"))
        if (auto it = bindings.inputs.find(*name); it != bindings.inputs.end())
            if (auto* t = dynamic_cast<ITextInput*>(root)) t->setInputHandler(it->second);

    if (const std::string* name = root->namedEvent("on_checked"))
        if (auto it = bindings.checkeds.find(*name); it != bindings.checkeds.end())
            if (auto* cb = dynamic_cast<UiCheckbox*>(root)) cb->setCheckedHandler(it->second);

    if (const std::string* name = root->namedEvent("on_value_changed"))
        if (auto it = bindings.valueChangeds.find(*name); it != bindings.valueChangeds.end())
            if (auto* s = dynamic_cast<UiSlider*>(root)) s->setValueChangedHandler(it->second);

    // 递归处理子节点
    for (auto& c : root->childrenMut())
        bindEvents(c.get(), bindings);
}
