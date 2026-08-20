/**
 * TextRenderer —— 文字渲染器
 *
 * 继承自 Shape，复用 Pipeline2D 的填充矩形管线。
 * 把字符串中每个字符的 "on" 像素绘制为小矩形，组成位图文字。
 *
 * 坐标系：与 Shape 一致，使用 NDC（屏幕坐标需调用者先转换）。
 * 但提供 setPixelPosition 用像素坐标 + 窗口尺寸进行转换的便捷接口。
 */
#pragma once

#include "../shapes/shape.h"
#include "bitmap_font.h"
#include <string>
#include <cstdint>

class TextRenderer : public Shape
{
public:
    TextRenderer() = default;
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

private:
    void generateVertices() override;

    std::string m_text;
    // 文字基线起点（NDC）
    float m_x = -0.9f;
    float m_y = 0.8f;
    // 每个字体像素在 NDC 中的大小
    float m_charSize = 0.04f;
    float m_r = 1.0f, m_g = 1.0f, m_b = 1.0f;
};
