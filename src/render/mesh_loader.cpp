/**
 * mesh_loader.cpp —— OBJ 模型加载实现
 */
#include "render/mesh_loader.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace obj_loader {

namespace {

// 解析 "1/2/3"、"1//3"、"1" 等索引串；返回 -1 表示缺省
struct FaceRef { int v = -1, vt = -1, vn = -1; };

bool parseFaceRef(const std::string& tok, FaceRef& out)
{
    std::string v, vt, vn;
    size_t s1 = tok.find('/');
    if (s1 == std::string::npos)
        v = tok;
    else
    {
        v = tok.substr(0, s1);
        size_t s2 = tok.find('/', s1 + 1);
        if (s2 == std::string::npos)
            vt = tok.substr(s1 + 1);
        else
        {
            vt = tok.substr(s1 + 1, s2 - s1 - 1);
            vn = tok.substr(s2 + 1);
        }
    }
    auto toInt = [](const std::string& s) {
        if (s.empty()) return -1;
        try { return std::stoi(s); } catch (...) { return -1; }
    };
    out.v = toInt(v); out.vt = toInt(vt); out.vn = toInt(vn);
    return out.v > 0 || out.v < 0;   // v 必须有效（>0 或相对负值）
}

// 相对索引转绝对
int resolve(int idx, int count)
{
    if (idx > 0) return idx - 1;
    if (idx < 0) return count + idx;   // 负数相对当前末尾
    return -1;
}

glm::vec3 computeFaceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    glm::vec3 ab = b - a, ac = c - a;
    glm::vec3 n = glm::cross(ab, ac);
    float len = glm::length(n);
    return len > 1e-8f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

bool load(const std::string& path, Mesh& out)
{
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return loadFromText(ss.str(), out);
}

bool loadFromText(const std::string& text, Mesh& out)
{
    Mesh mesh;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    bool hasNormals = false, hasUV = false;

    // 顶点去重表：以原始 (v,vt,vn) 索引三元组为键 → 已生成顶点下标。
    // 相同索引组合必然产生相同顶点，用哈希表达到 O(1) 查找，
    // 替代原先逐顶点线性扫描的 O(n²) 去重。
    struct IndexKey { int v, vt, vn; };
    struct IndexKeyHash {
        size_t operator()(const IndexKey& k) const noexcept
        {
            size_t h = std::hash<int>()(k.v);
            h = h * 31 + std::hash<int>()(k.vt);
            h = h * 31 + std::hash<int>()(k.vn);
            return h;
        }
    };
    struct IndexKeyEq {
        bool operator()(const IndexKey& a, const IndexKey& b) const noexcept
        { return a.v == b.v && a.vt == b.vt && a.vn == b.vn; }
    };
    std::unordered_map<IndexKey, uint32_t, IndexKeyHash, IndexKeyEq> vertexMap;

    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        // 去掉行尾 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "o" || kind == "g")
        {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && mesh.name.empty())
            {
                size_t b = rest.find_first_not_of(" \t");
                if (b != std::string::npos)
                {
                    size_t e = rest.find_last_not_of(" \t");
                    mesh.name = rest.substr(b, e - b + 1);
                }
            }
        }
        else if (kind == "v")
        {
            glm::vec3 p;
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (kind == "vt")
        {
            glm::vec2 t;
            ls >> t.x >> t.y;
            texcoords.push_back(t);
            hasUV = true;
        }
        else if (kind == "vn")
        {
            glm::vec3 n;
            ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
            hasNormals = true;
        }
        else if (kind == "f")
        {
            std::string tok;
            std::vector<FaceRef> refs;
            while (ls >> tok)
            {
                FaceRef r;
                if (parseFaceRef(tok, r)) refs.push_back(r);
            }
            if (refs.size() < 3) continue;

            // 三角化：fan（多边形假设凸）
            for (size_t i = 1; i + 1 < refs.size(); ++i)
            {
                FaceRef tri[3] = { refs[0], refs[i], refs[i + 1] };
                uint32_t triIdx[3];
                for (int k = 0; k < 3; ++k)
                {
                    int vi = resolve(tri[k].v, static_cast<int>(positions.size()));
                    int ti = resolve(tri[k].vt, static_cast<int>(texcoords.size()));
                    int ni = resolve(tri[k].vn, static_cast<int>(normals.size()));

                    // 越界索引防护（损坏/恶意 OBJ）：任何引用超出数组范围即失败
                    if (vi < 0 || vi >= static_cast<int>(positions.size()) ||
                        (ti >= 0 && ti >= static_cast<int>(texcoords.size())) ||
                        (ni >= 0 && ni >= static_cast<int>(normals.size())))
                    { mesh.vertices.clear(); mesh.indices.clear(); return false; }

                    IndexKey key{ tri[k].v, tri[k].vt, tri[k].vn };
                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end())
                    {
                        triIdx[k] = it->second;
                        continue;
                    }
                    Vertex vx;
                    vx.position = positions[vi];
                    if (ti >= 0) vx.uv = texcoords[ti];
                    if (ni >= 0) vx.normal = normals[ni];
                    triIdx[k] = static_cast<uint32_t>(mesh.vertices.size());
                    vertexMap.emplace(key, triIdx[k]);
                    mesh.vertices.push_back(vx);
                }
                mesh.indices.push_back(triIdx[0]);
                mesh.indices.push_back(triIdx[1]);
                mesh.indices.push_back(triIdx[2]);
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty())
        return false;

    // 法线缺失 → 平面法线
    if (!hasNormals)
    {
        std::vector<glm::vec3> faceNormals(mesh.indices.size() / 3);
        for (size_t i = 0; i < faceNormals.size(); ++i)
        {
            const Vertex& a = mesh.vertices[mesh.indices[i * 3 + 0]];
            const Vertex& b = mesh.vertices[mesh.indices[i * 3 + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[i * 3 + 2]];
            faceNormals[i] = computeFaceNormal(a.position, b.position, c.position);
        }
        std::vector<glm::vec3> acc(mesh.vertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i < faceNormals.size(); ++i)
            for (int k = 0; k < 3; ++k)
                acc[mesh.indices[i * 3 + k]] += faceNormals[i];
        for (size_t v = 0; v < mesh.vertices.size(); ++v)
        {
            float len = glm::length(acc[v]);
            if (len > 1e-8f) mesh.vertices[v].normal = acc[v] / len;
            else mesh.vertices[v].normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    mesh.hasNormals = hasNormals;
    mesh.hasUV      = hasUV;
    out = std::move(mesh);
    return true;
}

} // namespace obj_loader
