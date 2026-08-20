/**
 * BitmapFont —— 手写 5x7 点阵字体（自包含，无外部依赖）
 *
 * 字体覆盖范围：ASCII 32~95（空格、数字、大写字母、常见标点）。
 * 每个字符 5 列 × 7 行，使用 7 字节表示（每字节低 5 位为列）。
 *
 * 通过 TextRenderer 把每个 "on" 像素绘制为一个小矩形，
 * 复用现有 2D 矩形管线，无需纹理/采样器，实现简单可控。
 */
#pragma once

#include <cstdint>
#include <array>
#include <string>

class BitmapFont
{
public:
    static constexpr int GLYPH_W = 5;
    static constexpr int GLYPH_H = 7;
    static constexpr int GLYPH_ADVANCE = GLYPH_W + 1; // 字间距 1 像素

    // 获取字符 ch 的 7 字节位图；不可见字符返回全 0
    static std::array<uint8_t, GLYPH_H> get(char ch);

    // 是否为可渲染字符
    static bool isPrintable(char ch);
};
