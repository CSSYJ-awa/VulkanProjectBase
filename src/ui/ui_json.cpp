/**
 * UiJson 实现
 */
#include "ui_json.h"

#include <cctype>
#include <stdexcept>
#include <sstream>
#include <cmath>

namespace
{

class Parser
{
public:
    Parser(const std::string& t) : m_text(t) {}

    JsonPtr parse()
    {
        skipWs();
        JsonPtr v = parseValue();
        skipWs();
        // 允许尾部空白
        return v;
    }

private:
    const std::string& m_text;
    size_t m_pos = 0;

    [[noreturn]] void fail(const std::string& msg)
    {
        throw std::runtime_error("JSON 解析错误 @ " +
            std::to_string(m_pos) + ": " + msg);
    }

    char peek() { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }
    char next() { return m_pos < m_text.size() ? m_text[m_pos++] : '\0'; }

    void skipWs()
    {
        while (m_pos < m_text.size())
        {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++m_pos;
            }
            else if (c == '/' && m_pos + 1 < m_text.size())
            {
                // 支持 JSONC 风格注释：'//' 行注释 和 '/* */' 块注释
                char next = m_text[m_pos + 1];
                if (next == '/')
                {
                    // 行注释：跳到行尾
                    m_pos += 2;
                    while (m_pos < m_text.size() &&
                           m_text[m_pos] != '\n' && m_text[m_pos] != '\r')
                        ++m_pos;
                }
                else if (next == '*')
                {
                    // 块注释：跳到 '*/'
                    m_pos += 2;
                    while (m_pos + 1 < m_text.size() &&
                           !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/'))
                        ++m_pos;
                    if (m_pos + 1 < m_text.size()) m_pos += 2;
                    else fail("块注释 '/*' 未闭合");
                }
                else
                {
                    break;  // 单 '/' 不是注释，留给后续解析处理（通常会失败）
                }
            }
            else
            {
                break;
            }
        }
    }

    bool match(char c)
    {
        skipWs();
        if (m_pos < m_text.size() && m_text[m_pos] == c)
        {
            ++m_pos;
            return true;
        }
        return false;
    }

    void expect(char c)
    {
        if (!match(c))
            fail(std::string("期望 '") + c + "'");
    }

    JsonPtr parseValue()
    {
        skipWs();
        if (m_pos >= m_text.size()) fail("意外的输入结束");
        char c = m_text[m_pos];
        switch (c)
        {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return parseString();
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }

    JsonPtr parseObject()
    {
        auto v = std::make_unique<JsonValue>();
        v->type = JsonValue::Type::Object;
        expect('{');
        skipWs();
        if (match('}')) return v;

        for (;;)
        {
            skipWs();
            // key
            std::string key = parseRawString();
            expect(':');
            JsonPtr val = parseValue();
            v->obj[std::move(key)] = std::move(val);
            if (match(',')) continue;
            expect('}');
            break;
        }
        return v;
    }

    JsonPtr parseArray()
    {
        auto v = std::make_unique<JsonValue>();
        v->type = JsonValue::Type::Array;
        expect('[');
        skipWs();
        if (match(']')) return v;
        for (;;)
        {
            JsonPtr val = parseValue();
            v->arr.push_back(std::move(val));
            if (match(',')) continue;
            expect(']');
            break;
        }
        return v;
    }

    JsonPtr parseString()
    {
        auto v = std::make_unique<JsonValue>();
        v->type = JsonValue::Type::String;
        v->s = parseRawString();
        return v;
    }

    std::string parseRawString()
    {
        skipWs();
        expect('"');
        std::string out;
        while (m_pos < m_text.size())
        {
            char c = m_text[m_pos++];
            if (c == '"') return out;
            if (c == '\\')
            {
                if (m_pos >= m_text.size()) fail("字符串转义未完成");
                char e = m_text[m_pos++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        // 简化：跳过 4 位十六进制，转 UTF-8 字节
                        if (m_pos + 4 > m_text.size()) fail("\\u 转义不完整");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            char h = m_text[m_pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                            else fail("\\u 转义含非法字符");
                        }
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800)
                        {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail("未知字符串转义 \\" + std::string(1, e));
                }
            }
            else
            {
                out += c;
            }
        }
        fail("字符串未闭合");
    }

    JsonPtr parseBool()
    {
        auto v = std::make_unique<JsonValue>();
        v->type = JsonValue::Type::Bool;
        if (m_text.compare(m_pos, 4, "true") == 0)
        {
            v->b = true; m_pos += 4;
        }
        else if (m_text.compare(m_pos, 5, "false") == 0)
        {
            v->b = false; m_pos += 5;
        }
        else fail("非法字面量");
        return v;
    }

    JsonPtr parseNull()
    {
        auto v = std::make_unique<JsonValue>();
        v->type = JsonValue::Type::Null;
        if (m_text.compare(m_pos, 4, "null") == 0) m_pos += 4;
        else fail("非法字面量");
        return v;
    }

    JsonPtr parseNumber()
    {
        size_t start = m_pos;
        if (peek() == '-') ++m_pos;
        while (m_pos < m_text.size())
        {
            char c = m_text[m_pos];
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                ++m_pos;
            else break;
        }
        std::string numStr = m_text.substr(start, m_pos - start);
        try
        {
            std::size_t idx = 0;
            double d = std::stod(numStr, &idx);
            auto v = std::make_unique<JsonValue>();
            v->type = JsonValue::Type::Number;
            v->n = d;
            return v;
        }
        catch (...) { fail("非法数字: " + numStr); }
    }
};

} // namespace

// ---- JsonValue 访问 ----

const JsonValue* JsonValue::find(const std::string& key) const
{
    auto it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return it->second.get();
}

std::string JsonValue::asString(const std::string& def) const
{
    return (type == Type::String) ? s : def;
}

double JsonValue::asNumber(double def) const
{
    return (type == Type::Number) ? n : def;
}

bool JsonValue::asBool(bool def) const
{
    return (type == Type::Bool) ? b : def;
}

// ---- 顶层解析入口 ----

JsonPtr parseJson(const std::string& text)
{
    try
    {
        Parser p(text);
        return p.parse();
    }
    catch (const std::exception& e)
    {
        // 失败返回 nullptr
        return nullptr;
    }
}
