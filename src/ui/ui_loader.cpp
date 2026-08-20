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
    if (type == "button")  e = std::make_unique<UiButton>();
    else if (type == "text") e = std::make_unique<UiText>();
    else if (type == "textbox") e = std::make_unique<UiTextBox>();
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
            ; // IDraggable 默认即可拖拽，draggable 标志仅作记录
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
}
