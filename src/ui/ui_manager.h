/**
 * UiManager —— UI 管理
 *
 * 职责：
 *   1. 持有 UI 元素树的根节点
 *   2. 接收 GLFW 鼠标/键盘输入，转换为 UiEvent 分发给树
 *   3. 每帧遍历树进行绘制
 *   4. 处理拖拽面板的逐帧更新（按住时移动）
 *   5. 支持 hover 状态跟踪
 */
#pragma once

#include "ui_element.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

class UiManager
{
public:
    UiManager() = default;
    ~UiManager() = default;

    // 装载 UI 配置（JSON 文件），返回是否成功
    bool loadFromFile(const std::string& path);

    // 直接装载根节点（用于代码内构建）
    void setRoot(std::unique_ptr<UiElement> root);

    UiElement* root() const { return m_root.get(); }

    // 输入：每帧调用以更新 hover / drag 状态
    void onMouseMove(double px, double py);
    void onMouseButton(int button, int action, int mods);
    void onKey(int key, int scancode, int action, int mods);
    void onChar(unsigned int codepoint);

    // 每帧绘制
    void draw(const UiRenderContext& ctx);

    // 每帧更新（拖拽等）
    void update(float dt);

private:
    std::unique_ptr<UiElement> m_root;
    double m_mouseX = 0, m_mouseY = 0;
    bool   m_leftDown = false;
    UiElement* m_dragTarget = nullptr; // 正在拖拽的面板
    UiElement* m_hovered   = nullptr; // 当前悬停元素
};
