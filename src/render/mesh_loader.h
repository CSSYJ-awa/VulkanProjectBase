/**
 * mesh_loader.h —— OBJ 模型加载（渲染引擎 v1.0.1 扩展模块）
 *
 * 功能：
 *   - 解析 Wavefront OBJ 文本：v（顶点）/ vt（UV）/ vn（法线）/ f（面）
 *   - 支持三角形与四边形面、负索引（相对）、缺失法线时自动计算（平面着色）
 *   - 纯 CPU 解析，无 Vulkan 依赖，输出结构化 Mesh 数据
 *
 * 使用：解析得到的 vertices/indices 可直接喂给 Mesh3D 体系或自定义管线
 * （需自行转换为渲染格式；uv 供贴图管线使用）。
 */
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// obj_loader —— OBJ 解析
// ============================================================================
namespace obj_loader {

struct Vertex
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 normal  {0.0f, 0.0f, 1.0f};
    glm::vec2 uv      {0.0f, 0.0f};
};

struct Mesh
{
    std::string            name;
    std::vector<Vertex>    vertices;
    std::vector<uint32_t>  indices;
    bool hasNormals = false;   // 源文件是否含 vn（false = 已自动平面法线）
    bool hasUV      = false;   // 源文件是否含 vt

    size_t vertexCount() const { return vertices.size(); }
    size_t indexCount()  const { return indices.size(); }
    bool   valid()       const { return !vertices.empty() && !indices.empty(); }
};

// 解析 OBJ 文件。成功返回 true；失败返回 false（out 保持原状）。
bool load(const std::string& path, Mesh& out);

// 解析 OBJ 文本（内存字符串）。
bool loadFromText(const std::string& text, Mesh& out);

} // namespace obj_loader
