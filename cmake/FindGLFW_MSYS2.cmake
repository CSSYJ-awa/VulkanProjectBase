# ==============================================================================
# FindGLFW_MSYS2.cmake
#
# 用途：当标准 find_package(glfw3) 失败时，自动在 MSYS2 安装路径下搜索 GLFW。
#
# 背景：
#   MSYS2 将 GLFW 安装在 /mingw64/（或 /ucrt64/、/clang64/）下，
#   但从 Windows 原生命令行启动的 CMake 不会搜索这些 Unix 风格路径。
#   本模块复用 FindMSYS2.cmake 推断根目录，并用 find_library / find_path 完成
#   头文件与库的定位，创建 IMPORTED 目标 glfw 供 target_link_libraries 使用。
#
# 调用方式：
#   if(NOT glfw3_FOUND)
#       include(cmake/FindGLFW_MSYS2.cmake)
#   endif()
#
# 输出变量（与 find_package(glfw3) 兼容）：
#   GLFW_FOUND          - TRUE 或 FALSE
#   GLFW_INCLUDE_DIRS   - 头文件路径
#   GLFW_LIBRARIES      - 库文件完整路径
#   GLFW_LIBRARY        - 同 GLFW_LIBRARIES（兼容旧变量名）
#   GLFW_RUNTIME_DLL    - 动态链接时对应的 glfw3.dll 路径（静态链接时为空）
#
# 复用宏（供主 CMakeLists.txt 第 3 层回退调用，避免重复实现）：
#   glfw_make_imported_target()      - 创建 glfw IMPORTED 目标
#   glfw_locate_runtime_dll(_LIB)   - 从 .dll.a 反推 glfw3.dll 路径
# ==============================================================================

# ---- 共享宏：必须定义在所有 return 之前，确保主 CMakeLists.txt 第 3 层
#      回退调用时宏已就绪（即使本模块因非 MinGW / MSYS2 未找到而提前 return） ----

# 从 .dll.a 库路径反推对应的 glfw3.dll（用于 post-build 复制）
macro(glfw_locate_runtime_dll _LIB_VAR)
    set(GLFW_RUNTIME_DLL "")
    if(${_LIB_VAR} MATCHES "\\.dll\\.a$")
        get_filename_component(_LIB_DIR    "${${_LIB_VAR}}" DIRECTORY)
        get_filename_component(_LIB_PARENT "${_LIB_DIR}"    DIRECTORY)
        set(_DLL_CAND "${_LIB_PARENT}/bin/glfw3.dll")
        if(EXISTS "${_DLL_CAND}")
            set(GLFW_RUNTIME_DLL "${_DLL_CAND}" CACHE FILEPATH "GLFW 运行时 DLL")
            message(STATUS "[GLFW] 运行时 DLL: ${_DLL_CAND}")
        endif()
    endif()
endmacro()

# 创建 IMPORTED 目标 glfw（已存在则跳过）
macro(glfw_make_imported_target)
    if(GLFW_LIBRARY AND GLFW_INCLUDE_DIRS AND NOT TARGET glfw)
        add_library(glfw UNKNOWN IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_DIRS}"
        )
    endif()
endmacro()

# ---- include guard：避免 CheckEnvironment 与主流程重复 include 导致日志重复 ----
#      宏定义在此 guard 之前，确保无论是否已加载，宏都对主流程可用。
if(_GLFW_MSYS2_LOADED)
    return()
endif()
set(_GLFW_MSYS2_LOADED TRUE)

# ---- 仅 MinGW 环境 ----
if(NOT MINGW)
    message(STATUS "[GLFW MSYS2] 非 MinGW 编译器，跳过 MSYS2 检测。")
    return()
endif()

# ---- 复用共享 MSYS2 探测模块 ----
include(cmake/FindMSYS2.cmake)
if(NOT MSYS2_FOUND)
    return()
endif()

# ============================================================================
# 在 MSYS2 各子系统下搜索 GLFW
# ============================================================================

# 按子系统优先级构造 lib / include 路径列表
set(_GLFW_LIB_PATHS "")
set(_GLFW_INC_PATHS "")
foreach(_SUB ${MSYS2_SEARCH_SUBSYSTEMS})
    list(APPEND _GLFW_LIB_PATHS "${MSYS2_ROOT_DIR}/${_SUB}/lib")
    list(APPEND _GLFW_INC_PATHS "${MSYS2_ROOT_DIR}/${_SUB}/include")
endforeach()

# ---- 搜索库（优先静态库 .a，其次动态导入库 .dll.a）----
# 静态库优先：避免运行时对 glfw3.dll 的依赖，便于分发。
# MinGW 下 CMAKE_FIND_LIBRARY_SUFFIXES 默认为 ".dll.a;.a"，此处临时调换顺序。
set(_GLFW_OLD_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll.a" ".lib")
find_library(GLFW_LIBRARY
    NAMES glfw3 glfw3dll glfw
    PATHS ${_GLFW_LIB_PATHS}
    DOC "GLFW 静态/导入库路径 (MSYS2)"
    NO_DEFAULT_PATH
)
set(CMAKE_FIND_LIBRARY_SUFFIXES "${_GLFW_OLD_SUFFIXES}")

# ---- 搜索头文件 ----
find_path(GLFW_INCLUDE_DIRS
    NAMES GLFW/glfw3.h
    PATHS ${_GLFW_INC_PATHS}
    DOC "GLFW 头文件路径 (MSYS2)"
    NO_DEFAULT_PATH
)

if(GLFW_LIBRARY AND GLFW_INCLUDE_DIRS)
    set(GLFW_LIBRARIES "${GLFW_LIBRARY}")
    set(GLFW_FOUND TRUE)

    glfw_make_imported_target()
    glfw_locate_runtime_dll(GLFW_LIBRARY)

    # 识别库类型用于日志（先判 .dll.a，否则纯 .a 会被 .a$ 误匹配为"静态"）
    if(GLFW_LIBRARY MATCHES "\\.dll\\.a$")
        set(_LIB_TYPE "动态导入库")
    elseif(GLFW_LIBRARY MATCHES "\\.a$")
        set(_LIB_TYPE "静态库")
    else()
        set(_LIB_TYPE "库")
    endif()

    message(STATUS "[GLFW MSYS2] 找到 ${_LIB_TYPE}: ${GLFW_LIBRARY}")
    message(STATUS "[GLFW MSYS2] 头文件: ${GLFW_INCLUDE_DIRS}")
else()
    message(WARNING "[GLFW MSYS2] 在 ${MSYS2_ROOT_DIR} 下未找到 GLFW。\n"
                    "  请通过 MSYS2 安装 GLFW（按你的 MinGW 子系统选择）：\n"
                    "    · pacman -S mingw-w64-ucrt-x86_64-glfw       # ucrt64\n"
                    "    · pacman -S mingw-w64-x86_64-glfw            # mingw64\n"
                    "    · pacman -S mingw-w64-clang-x86_64-glfw       # clang64\n"
                    "  或运行 scripts\\setup_env.ps1 自动部署环境。")
endif()
