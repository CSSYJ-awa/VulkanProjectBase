/**
 * Font 实现
 *
 * PixelFont：封装内置 BitmapFont（5×7 ASCII），返回 0 或 255 的 alpha
 * SmoothFont：用 Windows GDI 的 GetGlyphOutlineA(GGO_GRAY8_BITMAP) 光栅化 TTF
 *             字符 alpha 范围归一化为 0~255，每字符嵌入到统一 cellH 的单元中
 * FontRegistry：单例，按名称注册/查找，getOrCreateSmoothFont 一站式封装
 */
#include "font.h"
#include "bitmap_font.h"
#include "../engine/logger.h"

// CMakeLists.txt 全局定义了 NOGDI（避免 Rectangle/Polygon 等 GDI 宏污染）
// 但 SmoothFont 需要 GDI 函数（CreateFontA/GetGlyphOutlineA 等），
// 因此局部取消 NOGDI 并显式包含 wingdi.h。
#ifdef NOGDI
#  undef NOGDI
#  define VULKAN_PROJECT_RESTORE_NOGDI
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <wingdi.h>
#ifdef VULKAN_PROJECT_RESTORE_NOGDI
#  define NOGDI
#endif
#include <cstring>

// ============================================================================
// PixelFont —— 内置像素字单例
// ============================================================================

PixelFont* PixelFont::instance()
{
    static PixelFont inst;
    return &inst;
}

const std::string& PixelFont::name() const
{
    return m_name;
}

int PixelFont::glyphWidth(char /*c*/) const
{
    return BitmapFont::GLYPH_W;  // 5
}

int PixelFont::glyphHeight() const
{
    return BitmapFont::GLYPH_H;  // 7
}

int PixelFont::advance(char c) const
{
    if (c == ' ' || !BitmapFont::isPrintable(c)) return BitmapFont::GLYPH_ADVANCE;
    return BitmapFont::GLYPH_ADVANCE;  // 像素字固定字间距 6
}

uint8_t PixelFont::pixelAlpha(char c, int row, int col) const
{
    if (row < 0 || row >= BitmapFont::GLYPH_H) return 0;
    if (col < 0 || col >= BitmapFont::GLYPH_W) return 0;
    if (!BitmapFont::isPrintable(c)) return 0;
    auto glyph = BitmapFont::get(c);
    uint8_t bits = glyph[row];
    // 列 0 在最左，对应位 0
    return (bits & (1 << col)) ? 255 : 0;
}

bool PixelFont::isPrintable(char c) const
{
    return BitmapFont::isPrintable(c);
}

// ============================================================================
// SmoothFont —— Windows GDI TTF 光栅化
// ============================================================================

SmoothFont::SmoothFont(const std::string& fontName, int pixelSize)
    : m_fontName(fontName), m_pixelSize(pixelSize)
{
    // 注册名 = fontName + "_" + pixelSize（如 "arial_16"）
    // 转小写以方便查找
    m_name = m_fontName;
    for (auto& ch : m_name) if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
    m_name += "_" + std::to_string(pixelSize);

    rasterizeAll();
    if (!m_valid)
    {
        LOG_WARN("Text", "SmoothFont", "无法创建字体 \"%s\" (%dpx)，将回退到默认 PixelFont",
                 m_fontName.c_str(), pixelSize);
    }
    else
    {
        LOG_INFO("Text", "SmoothFont", "字体 \"%s\" %dpx 创建成功，cellH=%d",
                 m_fontName.c_str(), pixelSize, m_cellH);
    }
}

SmoothFont::~SmoothFont() = default;

bool SmoothFont::rasterizeGlyph(char c, GlyphData& out)
{
    HDC hdc = GetDC(nullptr);
    if (!hdc) return false;

    // 创建逻辑字体（ClearType 抗锯齿）
    HFONT hFont = CreateFontA(
        -m_pixelSize,            // 高度（负值表示 character height，更接近用户期望）
        0,                       // 宽度自动
        0, 0,                    // esc/clsc
        FW_NORMAL,               // 字重
        FALSE, FALSE, FALSE,    // italic/underline/strikeout
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,       // 启用 ClearType 抗锯齿
        DEFAULT_PITCH | FF_SWISS,
        m_fontName.c_str()
    );
    if (!hFont)
    {
        ReleaseDC(nullptr, hdc);
        return false;
    }

    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    // 获取 TEXTMETRIC 以确定 cellH 和 ascent
    TEXTMETRICA tm = {};
    if (!GetTextMetricsA(hdc, &tm))
    {
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdc);
        return false;
    }
    m_cellH = static_cast<int>(tm.tmHeight) + static_cast<int>(tm.tmExternalLeading);
    if (m_cellH <= 0) m_cellH = tm.tmHeight;

    // 获取字符 glyph 位图（GGO_GRAY8_BITMAP = 8-bit 抗锯齿，值 0~64）
    MAT2 mat2 = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };  // 单位矩阵
    GLYPHMETRICS gm = {};
    DWORD size = GetGlyphOutlineA(
        hdc,
        static_cast<UINT>(static_cast<unsigned char>(c)),
        GGO_GRAY8_BITMAP,
        &gm, 0, nullptr, &mat2
    );

    if (size == GDI_ERROR || size == 0)
    {
        // 字符不可用（如控制字符）→ 空 glyph
        out.alpha.assign(gm.gmCellIncX * m_cellH, 0);
        out.cellW = gm.gmCellIncX > 0 ? gm.gmCellIncX : m_pixelSize / 2;
        out.valid = true;
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdc);
        return true;
    }

    std::vector<uint8_t> rawBuf(size);
    if (GetGlyphOutlineA(
            hdc,
            static_cast<UINT>(static_cast<unsigned char>(c)),
            GGO_GRAY8_BITMAP,
            &gm, size, rawBuf.data(), &mat2) == GDI_ERROR)
    {
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdc);
        return false;
    }

    // 把 black box 嵌入到 cell 中（cellW × cellH）
    int cellW = static_cast<int>(gm.gmCellIncX);
    if (cellW <= 0) cellW = m_pixelSize / 2;
    int boxW = static_cast<int>(gm.gmBlackBoxX);
    int boxH = static_cast<int>(gm.gmBlackBoxY);

    // black box 左上角在 cell 中的位置
    int offsetX = static_cast<int>(gm.gmptGlyphOrigin.x);
    int offsetY = static_cast<int>(tm.tmAscent) - static_cast<int>(gm.gmptGlyphOrigin.y);

    out.cellW = cellW;
    out.alpha.assign(static_cast<size_t>(cellW) * m_cellH, 0);

    // GGO_GRAY8_BITMAP 的行 stride 是 DWORD 对齐（4 字节倍）
    int rowStride = ((boxW + 3) / 4) * 4;
    for (int y = 0; y < boxH; ++y)
    {
        int dstY = offsetY + y;
        if (dstY < 0 || dstY >= m_cellH) continue;
        for (int x = 0; x < boxW; ++x)
        {
            int dstX = offsetX + x;
            if (dstX < 0 || dstX >= cellW) continue;
            uint8_t v = rawBuf[y * rowStride + x];
            // GGO_GRAY8_BITMAP 值范围 0~64（64 = 完全不透明），归一化为 0~255
            uint8_t alpha = static_cast<uint8_t>((static_cast<int>(v) * 255 + 32) / 64);
            out.alpha[static_cast<size_t>(dstY) * cellW + dstX] = alpha;
        }
    }

    out.valid = true;
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    ReleaseDC(nullptr, hdc);
    return true;
}

void SmoothFont::rasterizeAll()
{
    bool any = false;
    for (int i = 32; i < 127; ++i)  // 可打印 ASCII
    {
        char c = static_cast<char>(i);
        if (rasterizeGlyph(c, m_glyphs[i]))
            any = true;
    }
    m_valid = any;
}

int SmoothFont::glyphWidth(char c) const
{
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return m_pixelSize / 2;
    return m_glyphs[uc].cellW;
}

int SmoothFont::advance(char c) const
{
    return glyphWidth(c);
}

uint8_t SmoothFont::pixelAlpha(char c, int row, int col) const
{
    if (row < 0 || row >= m_cellH) return 0;
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) return 0;
    const GlyphData& g = m_glyphs[uc];
    if (col < 0 || col >= g.cellW) return 0;
    if (g.alpha.empty()) return 0;
    return g.alpha[static_cast<size_t>(row) * g.cellW + col];
}

bool SmoothFont::isPrintable(char c) const
{
    unsigned char uc = static_cast<unsigned char>(c);
    return uc >= 32 && uc < 127;
}

// ============================================================================
// FontRegistry —— 单例
// ============================================================================

FontRegistry::FontRegistry()
{
    // 内置像素字不需要所有权管理（单例 PixelFont::instance()）
}

FontRegistry& FontRegistry::instance()
{
    static FontRegistry inst;
    return inst;
}

Font* FontRegistry::registerFont(std::unique_ptr<Font> font)
{
    if (!font) return nullptr;
    Font* raw = font.get();
    m_fonts.push_back(std::move(font));
    return raw;
}

Font* FontRegistry::find(const std::string& name) const
{
    for (const auto& f : m_fonts)
        if (f->name() == name) return f.get();
    if (name == "pixel" || name == "default")
        return PixelFont::instance();
    return nullptr;
}

Font* FontRegistry::defaultFont() const
{
    return PixelFont::instance();
}

PixelFont* FontRegistry::pixelFont() const
{
    return PixelFont::instance();
}

Font* FontRegistry::getOrCreateSmoothFont(const std::string& fontName, int pixelSize)
{
    // 构造查找名（与 SmoothFont 构造函数中相同规则：lower + _size）
    std::string lookup = fontName;
    for (auto& ch : lookup) if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
    lookup += "_" + std::to_string(pixelSize);

    // 已注册则直接返回
    if (Font* existing = find(lookup)) return existing;

    // 创建新的 SmoothFont 并注册
    auto font = std::make_unique<SmoothFont>(fontName, pixelSize);
    if (!font->valid())
    {
        LOG_WARN("Text", "FontRegistry", "创建 \"%s\" %dpx 失败，回退到默认像素字",
                 fontName.c_str(), pixelSize);
        return PixelFont::instance();
    }
    return registerFont(std::move(font));
}
