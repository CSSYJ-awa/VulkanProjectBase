/**
 * UiLoader —— 从 JSON 构建 UI 元素树
 *
 * JSON 格式示例：
 * {
 *   "ui": {
 *     "type": "panel",
 *     "name": "root",
 *     "x": 0, "y": 0, "width": 200, "height": 600,
 *     "draggable": true,
 *     "color": [0.15, 0.15, 0.2, 0.85],
 *     "children": [
 *       {
 *         "type": "button",
 *         "name": "btn1",
 *         "x": 10, "y": 10, "width": 80, "height": 30,
 *         "text": "OK",
 *         "font_size": 14,
 *         "color": [0.3, 0.5, 0.8, 1.0]
 *       },
 *       {
 *         "type": "text",
 *         "name": "label",
 *         "x": 10, "y": 50, "width": 150, "height": 24,
 *         "text": "Hello UI",
 *         "color": [1, 1, 1, 0]
 *       },
 *       {
 *         "type": "textbox",
 *         "name": "input",
 *         "x": 10, "y": 90, "width": 150, "height": 30,
 *         "text": "type here"
 *       }
 *     ]
 *   }
 * }
 *
 * 支持 type：panel / button / text / textbox / slider / checkbox
 * 可选字段：on_xxx 保存事件名称（运行时由 UiLoader::bindEvents 绑定实际回调）：
 *   on_click / on_hover_enter / on_hover_leave / on_drag /
 *   on_input / on_checked / on_value_changed
 */
#pragma once

#include "ui_element.h"
#include "ui_event.h"
#include "ui_json.h"
#include <map>
#include <memory>
#include <string>
#include <functional>

// ============================================================================
// UiBindings —— 事件绑定表（事件名 → 回调）
//
// 与 JSON 中的 on_xxx 字段配合使用：
//   { "type":"button", "name":"btn_quit", "on_click":"quit" }
//   → 在 bindings.clicks 中查找 "quit"，绑定为按钮点击回调。
// 每种事件对应一个独立的表，类型安全地映射到对应控件接口。
// ============================================================================
struct UiBindings
{
    std::map<std::string, ClickHandler>                    clicks;          // on_click
    std::map<std::string, HoverHandler>                    hoverEnters;     // on_hover_enter
    std::map<std::string, HoverHandler>                    hoverLeaves;     // on_hover_leave
    std::map<std::string, DragHandler>                     drags;           // on_drag
    std::map<std::string, TextInputHandler>                inputs;          // on_input
    std::map<std::string, std::function<void(bool)>>       checkeds;        // on_checked   (UiCheckbox)
    std::map<std::string, std::function<void(float)>>      valueChangeds;   // on_value_changed (UiSlider)

    // 快捷注册：链式调用（可选，便于代码内紧凑配置）
    UiBindings& click(const std::string& name, ClickHandler h)            { clicks[name] = std::move(h); return *this; }
    UiBindings& hoverEnter(const std::string& name, HoverHandler h)       { hoverEnters[name] = std::move(h); return *this; }
    UiBindings& hoverLeave(const std::string& name, HoverHandler h)       { hoverLeaves[name] = std::move(h); return *this; }
    UiBindings& drag(const std::string& name, DragHandler h)              { drags[name] = std::move(h); return *this; }
    UiBindings& input(const std::string& name, TextInputHandler h)        { inputs[name] = std::move(h); return *this; }
    UiBindings& checked(const std::string& name, std::function<void(bool)> h) { checkeds[name] = std::move(h); return *this; }
    UiBindings& valueChanged(const std::string& name, std::function<void(float)> h) { valueChangeds[name] = std::move(h); return *this; }
};

class UiLoader
{
public:
    // 从 JSON 文本构建 UI 根节点，失败返回 nullptr
    static std::unique_ptr<UiElement> loadFromText(const std::string& json);

    // 从 JSON 文件构建
    static std::unique_ptr<UiElement> loadFromFile(const std::string& path);

    // 遍历元素树，把每个元素记录的 on_xxx 命名事件绑定到 bindings 表中对应回调。
    // 未被命名的元素 / 表中不存在的名称会被静默跳过。
    static void bindEvents(UiElement* root, const UiBindings& bindings);

private:
    // 从一个 JSON 对象节点构建元素（含子节点）
    static std::unique_ptr<UiElement> buildElement(const JsonValue& node);

    // 把通用属性（位置/尺寸/颜色/名称）应用到元素
    static void applyCommon(UiElement* e, const JsonValue& node);
};
