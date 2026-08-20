# ==============================================================================
# FindGLM_MSYS2.cmake
#
# 用途：当标准 find_package(glm) 失败时，自动在 MSYS2 安装路径下搜索 GLM。
#
# 背景：
#   GLM 是纯头文件库，MSYS2 将其安装在 /mingw64/include/glm/ 下。
#   本模块复用 FindMSYS2.cmake 推断根目录，仅用 find_path 定位头文件。
#
# 调用方式：
#   if(NOT glm_FOUND)
#       include(cmake/FindGLM_MSYS2.cmake)
#   endif()
#
# 输出变量：
#   GLM_FOUND          - TRUE 或 FALSE
#   GLM_INCLUDE_DIRS   - 头文件路径
# ==============================================================================

# ---- include guard：避免 CheckEnvironment 与主流程重复 include 导致日志重复 ----
if(_GLM_MSYS2_LOADED)
    return()
endif()
set(_GLM_MSYS2_LOADED TRUE)

# ---- 仅 MinGW 环境 ----
if(NOT MINGW)
    message(STATUS "[GLM MSYS2] 非 MinGW 编译器，跳过 MSYS2 检测。")
    return()
endif()

# ---- 复用共享 MSYS2 探测模块 ----
include(cmake/FindMSYS2.cmake)
if(NOT MSYS2_FOUND)
    return()
endif()

# ---- 按子系统优先级构造 include 路径列表 ----
set(_GLM_INC_PATHS "")
foreach(_SUB ${MSYS2_SEARCH_SUBSYSTEMS})
    list(APPEND _GLM_INC_PATHS "${MSYS2_ROOT_DIR}/${_SUB}/include")
endforeach()

# ---- 搜索头文件（GLM 纯头文件库，无需链接）----
find_path(GLM_INCLUDE_DIRS
    NAMES glm/glm.hpp
    PATHS ${_GLM_INC_PATHS}
    DOC "GLM 头文件路径 (MSYS2)"
    NO_DEFAULT_PATH
)

if(GLM_INCLUDE_DIRS)
    set(GLM_FOUND TRUE)
    set(glm_FOUND TRUE)  # 兼容 find_package(glm) 的变量名约定
    message(STATUS "[GLM MSYS2] 找到头文件: ${GLM_INCLUDE_DIRS}/glm/glm.hpp")
    message(STATUS "[GLM MSYS2] GLM 是纯头文件库，无需链接。")
else()
    message(WARNING "[GLM MSYS2] 在 ${MSYS2_ROOT_DIR} 下未找到 GLM。\n"
                    "  GLM 是纯头文件库，可通过以下方式安装（按子系统选择）：\n"
                    "    · pacman -S mingw-w64-ucrt-x86_64-glm       # ucrt64\n"
                    "    · pacman -S mingw-w64-x86_64-glm            # mingw64\n"
                    "    · pacman -S mingw-w64-clang-x86_64-glm       # clang64\n"
                    "  或手动下载 GLM: https://github.com/g-truc/glm/releases\n"
                    "  设置环境变量 GLM_INCLUDE_DIR 指向包含 glm/glm.hpp 的目录。")
endif()
