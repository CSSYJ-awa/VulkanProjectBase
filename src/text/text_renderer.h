/**
 * TextRenderer —— 文字渲染器
 *
 * 继承自 Shape，复用 Pipeline2D 的填充矩形管线。
 * 把字符串中每个字符的 "on" 像素绘制为小矩形，组成位图文字。
 *
 * v1.2 新增：支持 Font 抽象（Font*），可选 PixelFont 或 SmoothFont。
 *   - PixelFont：1-bit alpha，硬边缘像素字（默认）
 *   - SmoothFont：8-bit alpha，灰度抗锯齿（通过亮度调制进顶点色 RGB）
 *
 * 坐标系：与 Shape 一致，使用 NDC（屏幕坐标需调用者先转换）。
 * 但提供 setPixelPosition 用像素坐标 + 窗口尺寸进行转换的便捷接口。
 */
#pragma once

#include "../shapes/shape.h"
#include "bitmap_font.h"
#include "font.h"
#include <string>
#include <cstdint>

class TextRenderer : public Shape
{
public:
    TextRenderer();
    // 像素坐标版本：以 (px, py)（像素，左上为原点）作为文字起点，
    // pixelSize 为每个字体像素的屏幕像素大小，w/h 为窗口尺寸
    TextRenderer(const std::string& text, float px, float py,
                 uint32_t pixelSize, uint32_t w, uint32_t h,
                 float r = 1.0f, float g = 1.0f, float b = 1.0f);

    // NDC 版本
    TextRenderer(const std::string& text, float xNdc, float yNdc,
                 float ndcSize, float r = 1.0f, float g = 1.0f, float b = 1.0f);

    void setText(const std::string& text);
    void setPixelPosition(float px, float py, uint32_t w, uint32_t h);
    // 更新字体像素大小（窗口尺寸变化时需要重新计算 NDC 字号）
    void setPixelSize(uint32_t pixelSize, uint32_t w);

    // v1.2：设置字体（nullptr = 使用默认 PixelFont）
    // 设置后自动重算 m_charSize（用当前 m_pixelSize / m_screenW + 新字体 glyphHeight）
    void setFont(Font* font);
    Font* font() const { return m_font; }

private:
    void generateVertices() override;
    // 用当前 m_pixelSize/m_screenW/m_font 重算 m_charSize
    void recomputeCharSize();

    std::string m_text;
    // 文字基线起点（NDC）
    float m_x = -0.9f;
    float m_y = 0.8f;
    // 每个字体像素在 NDC 中的大小
    float m_charSize = 0.04f;
    float m_r = 1.0f, m_g = 1.0f, m_b = 1.0f;

    // v1.2：缓存 setPixelSize 的参数，便于字体切换时重算 m_charSize
    uint32_t m_pixelSize = 16;     // 期望字体总像素高度
    uint32_t m_screenW   = 1280;  // 上次 setPixelSize 时的窗口宽度

    // v1.2：字体指针（nullptr = 默认 PixelFont::instance()）
    // 不持有所有权，调用方需保证生命周期（FontRegistry / 全局 PixelFont 单例）
    Font* m_font = nullptr;
};
