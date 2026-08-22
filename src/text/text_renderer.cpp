/**
 * TextRenderer 实现
 *
 * v1.2：generateVertices 使用 Font 抽象层（Font::pixelAlpha），
 * 支持 PixelFont（1-bit）和 SmoothFont（8-bit alpha 亮度调制）。
 */
#include "text_renderer.h"
#include "font.h"

TextRenderer::TextRenderer()
{
    // 默认使用 PixelFont（m_font = nullptr 表示用 defaultFont）
}

TextRenderer::TextRenderer(const std::string& text, float px, float py,
                           uint32_t pixelSize, uint32_t w, uint32_t h,
                           float r, float g, float b)
    : m_text(text), m_r(r), m_g(g), m_b(b)
{
    setPixelPosition(px, py, w, h);
    setPixelSize(pixelSize, w);
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

void TextRenderer::recomputeCharSize()
{
    Font* f = m_font ? m_font : FontRegistry::instance().defaultFont();
    int gh = f->glyphHeight();
    if (gh <= 0) gh = 7;
    if (m_screenW > 0)
        m_charSize = 2.0f * m_pixelSize / (static_cast<float>(m_screenW) * gh);
}

void TextRenderer::setPixelSize(uint32_t pixelSize, uint32_t w)
{
    m_pixelSize = pixelSize;
    m_screenW   = w;
    recomputeCharSize();
    m_dirty = true;
}

void TextRenderer::setFont(Font* font)
{
    m_font = font;
    // 字体切换后 glyphHeight 可能不同（如 PixelFont=7, SmoothFont=14），
    // 自动重算 m_charSize 保证文字总像素高度仍为 m_pixelSize
    recomputeCharSize();
    m_dirty = true;
}

void TextRenderer::generateVertices()
{
    m_vertices.clear();

    // 获取字体（nullptr 则用默认 PixelFont）
    Font* f = m_font ? m_font : FontRegistry::instance().defaultFont();

    float cw = m_charSize;  // 单字体像素的 NDC 边长
    int   glyphH = f->glyphHeight();
    if (glyphH <= 0) glyphH = 7;

    float curX = m_x;
    float curY = m_y;  // 文字顶部 Y

    for (char ch : m_text)
    {
        if (ch == '\n')
        {
            // 换行：回到行首，向下推进一行（调试面板多行展示用）
            curX = m_x;
            curY += glyphH * cw;
            continue;
        }
        if (ch == ' ')
        {
            // 空格：用默认 advance 推进光标
            int adv = f->isPrintable(' ') ? f->advance(' ') : 6;
            curX += adv * cw;
            continue;
        }
        if (!f->isPrintable(ch))
        {
            // 不可打印字符跳过但推进光标
            curX += f->advance(ch) * cw;
            continue;
        }

        int adv  = f->advance(ch);     // 字符前进宽度（含间距）
        int gw  = f->glyphWidth(ch);    // 字符位图宽度

        for (int row = 0; row < glyphH; ++row)
        {
            for (int col = 0; col < gw; ++col)
            {
                uint8_t alpha = f->pixelAlpha(ch, row, col);
                if (alpha == 0) continue;

                // v1.2：alpha 通过亮度调制进顶点色 RGB
                // basic.frag: outColor = vec4(fragColor, 1.0) * pc.color
                // 对于 PixelFont（alpha=255）：r'=r, g'=g, b'=b（与原版完全一致）
                // 对于 SmoothFont（alpha=0~255）：r'=r*alpha/255, g'=g*alpha/255, ...
                //   视觉上表现为灰度抗锯齿（接近真实 alpha 混合）
                float norm = static_cast<float>(alpha) / 255.0f;
                float vr = m_r * norm;
                float vg = m_g * norm;
                float vb = m_b * norm;

                float px = curX + col * cw;
                float py = curY + row * cw;
                float x0 = px,     x1 = px + cw;
                float y0 = py,     y1 = py + cw;
                m_vertices.insert(m_vertices.end(), {
                    x0, y0, vr, vg, vb,
                    x1, y0, vr, vg, vb,
                    x1, y1, vr, vg, vb,
                    x0, y0, vr, vg, vb,
                    x1, y1, vr, vg, vb,
                    x0, y1, vr, vg, vb,
                });
            }
        }
        curX += adv * cw;
    }
}
