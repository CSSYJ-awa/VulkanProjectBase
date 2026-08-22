/**
 * Font —— 字体抽象层
 *
 * 设计目的：把"字符 → 像素 alpha 网格"的具体实现从 TextRenderer 解耦，
 * 支持多种字体（像素字 / 平滑字体 / 系统字体）通过同一接口渲染。
 *
 * 字体类型：
 *   - PixelFont：1-bit alpha（0 或 255），硬边缘像素字（内置 5×7 ASCII）
 *   - SmoothFont：8-bit alpha（0~255），抗锯齿平滑字体（Windows GDI TTF 光栅化）
 *
 * 渲染原理：TextRenderer 调用 font->pixelAlpha(ch, row, col) 获取每像素 alpha 值，
 * 把每个非零像素绘制成小矩形；alpha 通过亮度调制进顶点色 RGB
 * （basic.frag: outColor = vec4(fragColor, 1.0) * pc.color），
 * 平滑字体表现为灰度抗锯齿（视觉上接近真实抗锯齿）。
 *
 * FontRegistry 单例：按名称注册/查找字体，支持运行时切换。
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// 字体类型分类
enum class FontType
{
    Pixel,   // 1-bit alpha（0 或 255）：硬边缘像素字
    Smooth   // 8-bit alpha（0~255）：抗锯齿平滑字体
};

// Font 抽象基类
class Font
{
public:
    virtual ~Font() = default;

    // 字体注册名（用于 FontRegistry 查找，例如 "pixel"、"arial_16"）
    virtual const std::string& name() const = 0;

    // 字体类型
    virtual FontType type() const = 0;
    bool isSmooth() const { return type() == FontType::Smooth; }

    // 字符像素宽度（不含字间距；不同字符可能宽度不同，例如 'W' 比 'i' 宽）
    virtual int glyphWidth(char c) const = 0;
    // 字符像素高度（统一行高；像素字=7，平滑字=tmHeight）
    virtual int glyphHeight() const = 0;
    // 字符前进宽度（含字间距；用于光标前进）
    virtual int advance(char c) const = 0;

    // 获取字符 (row, col) 像素的 alpha 值（0~255）
    // PixelFont：返回 0 或 255
    // SmoothFont：返回 0~255（抗锯齿灰度）
    // 越界返回 0
    virtual uint8_t pixelAlpha(char c, int row, int col) const = 0;

    // 是否可打印（在字体覆盖范围内）
    virtual bool isPrintable(char c) const = 0;
};

// ============================================================================
// PixelFont —— 内置 6×8 像素字（封装 BitmapFont，单例）
// ============================================================================
class PixelFont : public Font
{
public:
    static PixelFont* instance();

    const std::string& name() const override;
    FontType type() const override { return FontType::Pixel; }
    int glyphWidth(char /*c*/) const override;  // 固定 5
    int glyphHeight() const override;            // 固定 7
    int advance(char c) const override;          // 6（含字间距）
    uint8_t pixelAlpha(char c, int row, int col) const override;
    bool isPrintable(char c) const override;

private:
    PixelFont() = default;
    std::string m_name = "pixel";
};

// ============================================================================
// SmoothFont —— 平滑字体（Windows GDI TTF 光栅化）
//
// 通过 CreateFontA + GetGlyphOutlineA(GGO_GRAY8_BITMAP) 获取抗锯齿位图。
// GDI 返回的 8-bit 灰度值范围是 0~64（非 0~255），内部归一化为 0~255。
//
// 字符位图布局：每个字符占据一个"字符单元"（cell），高度 = tmHeight（统一），
// 宽度 = gm.gmCellX（字符前进宽度，因字符而异）。GDI 返回的"黑色框"
// (black box) 是字符实际像素的范围，可能小于 cell；本类把 black box
// 嵌入到 cell 中，外圈补 0 alpha。
// ============================================================================
class SmoothFont : public Font
{
public:
    // fontName: Windows 系统字体名，例如 "Arial"、"Consolas"、"Microsoft YaHei"
    // pixelSize: 期望像素高度（实际高度可能略小，受字体度量影响）
    SmoothFont(const std::string& fontName, int pixelSize);
    ~SmoothFont() override;

    SmoothFont(const SmoothFont&) = delete;
    SmoothFont& operator=(const SmoothFont&) = delete;

    const std::string& name() const override { return m_name; }
    FontType type() const override { return FontType::Smooth; }
    int glyphWidth(char c) const override;        // cell 宽度（advance）
    int glyphHeight() const override { return m_cellH; }  // tmHeight
    int advance(char c) const override;
    uint8_t pixelAlpha(char c, int row, int col) const override;
    bool isPrintable(char c) const override;

    bool valid() const { return m_valid; }
    int  pixelSize() const { return m_pixelSize; }
    const std::string& systemFontName() const { return m_fontName; }

private:
    struct GlyphData
    {
        std::vector<uint8_t> alpha;  // cellW * cellH 的灰度 alpha（已归一化为 0~255）
        int cellW = 0;                // 字符前进宽度
        bool valid = false;
    };

    bool rasterizeGlyph(char c, GlyphData& out);
    void rasterizeAll();

    std::string          m_fontName;     // 系统字体名（如 "Arial"）
    std::string          m_name;          // 注册名 = fontName + "_" + pixelSize
    int                  m_pixelSize;     // 期望像素高度
    int                  m_cellH = 0;     // 实际字符单元高度（tmHeight）
    bool                 m_valid = false;
    GlyphData            m_glyphs[128];   // ASCII 0~127
};

// ============================================================================
// FontRegistry —— 字体注册表（单例）
//
// 用途：
//   1. 注册自定义字体（如加载其他 TTF 系统字体）
//   2. 按名称查找已注册字体
//   3. getOrCreateSmoothFont 一站式：若已注册则返回，否则用 GDI 创建并注册
// ============================================================================
class FontRegistry
{
public:
    static FontRegistry& instance();

    // 注册自定义字体（转移所有权），返回指针
    Font* registerFont(std::unique_ptr<Font> font);

    // 按名称查找已注册字体（找不到返回 nullptr）
    Font* find(const std::string& name) const;

    // 默认字体（PixelFont::instance()）
    Font* defaultFont() const;

    // 内置像素字快捷获取
    PixelFont* pixelFont() const;

    // 一站式获取平滑字体：若 "fontName_pixelSize" 已注册则返回，否则用 GDI 创建并注册
    // 失败（GDI 无法渲染该字体）时回退到默认 PixelFont 并发出警告
    Font* getOrCreateSmoothFont(const std::string& fontName, int pixelSize);

private:
    FontRegistry();
    std::vector<std::unique_ptr<Font>> m_fonts;
};
