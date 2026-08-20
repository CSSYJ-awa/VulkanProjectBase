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
 * 支持 type：panel / button / text / textbox
 * 可选字段：on_click 等保存事件名称（运行时由调用者绑定实际回调）
 */
#pragma once

#include "ui_element.h"
#include "ui_json.h"
#include <memory>
#include <string>

class UiLoader
{
public:
    // 从 JSON 文本构建 UI 根节点，失败返回 nullptr
    static std::unique_ptr<UiElement> loadFromText(const std::string& json);

    // 从 JSON 文件构建
    static std::unique_ptr<UiElement> loadFromFile(const std::string& path);

private:
    // 从一个 JSON 对象节点构建元素（含子节点）
    static std::unique_ptr<UiElement> buildElement(const JsonValue& node);

    // 把通用属性（位置/尺寸/颜色/名称）应用到元素
    static void applyCommon(UiElement* e, const JsonValue& node);
};
