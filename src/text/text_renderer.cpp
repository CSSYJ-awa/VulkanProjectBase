/**
 * TextRenderer 实现
 */
#include "text_renderer.h"

TextRenderer::TextRenderer(const std::string& text, float px, float py,
                           uint32_t pixelSize, uint32_t w, uint32_t h,
                           float r, float g, float b)
    : m_text(text), m_r(r), m_g(g), m_b(b)
{
    setPixelPosition(px, py, w, h);
    // pixelSize 为期望的字体总高度（像素），分配到每个字体像素
    m_charSize = 2.0f * pixelSize / (static_cast<float>(w) * BitmapFont::GLYPH_H);
}

TextRenderer::TextRenderer(const std::string& text, float xNdc, float yNdc,
                           float ndcSize, float r, float g, float b)
    : m_text(text), m_x(xNdc), m_y(yNdc), m_charSize(ndcSize),
      m_r(r), m_g(g), m_b(b)
{
}

void TextRenderer::setText(const std::string& text)
{
    m_text = text;
    m_dirty = true;
}

void TextRenderer::setPixelPosition(float px, float py, uint32_t w, uint32_t h)
{
    // NDC: x 从 px 转换（左 0 -> -1），y 从 py 转换（上 0 -> -1，Vulkan Y 向下）
    m_x = 2.0f * px / static_cast<float>(w) - 1.0f;
    m_y = 2.0f * py / static_cast<float>(h) - 1.0f;
    m_dirty = true;
}

void TextRenderer::setPixelSize(uint32_t pixelSize, uint32_t w)
{
    m_charSize = 2.0f * pixelSize / (static_cast<float>(w) * BitmapFont::GLYPH_H);
    m_dirty = true;
}

void TextRenderer::generateVertices()
{
    m_vertices.clear();
    // 每个字符 5 像素宽 + 1 像素间距
    float cw = m_charSize;           // 单字体像素的 NDC 边长
    float glyphW = BitmapFont::GLYPH_W * cw;
    float glyphH = BitmapFont::GLYPH_H * cw;
    float advance = glyphW + cw;      // 含间距

    float curX = m_x;
    float curY = m_y;                 // 文字顶部 Y
    for (char ch : m_text)
    {
        if (ch == ' ')
        {
            curX += advance;
            continue;
        }
        if (!BitmapFont::isPrintable(ch)) { curX += advance; continue; }

        auto glyph = BitmapFont::get(ch);
        for (int row = 0; row < BitmapFont::GLYPH_H; ++row)
        {
            uint8_t bits = glyph[row];
            for (int col = 0; col < BitmapFont::GLYPH_W; ++col)
            {
                if (bits & (1 << col))
                {
                    // col 0 在最左，位 0 对应列 0
                    float px = curX + col * cw;
                    // row 0 在顶部，Vulkan NDC Y 向下递增
                    float py = curY + row * cw;
                    // 填充一个小矩形（2 三角形）
                    float x0 = px,     x1 = px + cw;
                    float y0 = py,     y1 = py + cw;
                    m_vertices.insert(m_vertices.end(), {
                        x0, y0, m_r, m_g, m_b,
                        x1, y0, m_r, m_g, m_b,
                        x1, y1, m_r, m_g, m_b,
                        x0, y0, m_r, m_g, m_b,
                        x1, y1, m_r, m_g, m_b,
                        x0, y1, m_r, m_g, m_b,
                    });
                }
            }
        }
        curX += advance;
    }
}
