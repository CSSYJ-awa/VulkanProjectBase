/**
 * UiJson —— 极简 JSON 解析器（递归下降）
 *
 * 仅支持 UI 配置所需子集：object / array / string / number / bool / null。
 * 支持 JSONC 风格注释（// 行注释、/* 块注释）与尾随逗号。
 * 不支持科学计数法以外的数字语法。
 *
 * 用途：解析 ui_config.json / config.json，构建 UI 元素树或运行时配置。
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <variant>

class JsonValue;
using JsonPtr   = std::unique_ptr<JsonValue>;
using JsonArray = std::vector<JsonPtr>;
using JsonObject = std::map<std::string, JsonPtr>;

class JsonValue
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool        b = false;
    double      n = 0.0;
    std::string s;
    JsonArray   arr;
    JsonObject  obj;

    // 便捷访问
    bool        isNull()   const { return type == Type::Null; }
    bool        isObject() const { return type == Type::Object; }
    bool        isArray()  const { return type == Type::Array; }
    bool        isString() const { return type == Type::String; }
    bool        isNumber() const { return type == Type::Number; }
    bool        isBool()   const { return type == Type::Bool; }

    const JsonValue* find(const std::string& key) const;
    std::string asString(const std::string& def = "") const;
    double       asNumber(double def = 0.0) const;
    bool         asBool(bool def = false) const;
};

// 解析 JSON 文本，失败返回 nullptr
JsonPtr parseJson(const std::string& text);
