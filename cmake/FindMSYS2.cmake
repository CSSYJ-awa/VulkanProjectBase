# ==============================================================================
# FindMSYS2.cmake —— MSYS2 根目录与子系统共享探测模块
#
# 用途：统一推断 MSYS2 安装根目录与编译器所在子系统，供 FindGLFW_MSYS2 /
#       FindGLM_MSYS2 / CheckEnvironment / GenerateVulkanMingwLib 复用。
#
# 探测顺序：
#   1. 环境变量 MSYS2_ROOT（用户可手动指定）
#   2. 编译器路径反推（g++ 在 {root}/{subsystem}/bin/ 下，回退两级）
#   3. 常见安装路径扫描（C/D/E 盘的 msys64 / msys2）
#
# 输出变量：
#   MSYS2_ROOT_DIR         - MSYS2 根目录（CMake 正斜杠路径），未找到则为空
#   MSYS2_SUBSYSTEM        - 编译器所在子系统名（如 ucrt64），无法推断则为空
#   MSYS2_SEARCH_SUBSYSTEMS - 去重后的搜索顺序列表，编译器子系统优先
#   MSYS2_FOUND            - TRUE / FALSE
#
# 调用方式：
#   include(cmake/FindMSYS2.cmake)
#   if(MSYS2_FOUND)
#       ...
#   endif()
# ==============================================================================

# ---- include guard：避免重复加载 ----
if(DEFINED MSYS2_FOUND)
    return()
endif()

set(MSYS2_FOUND FALSE)
set(MSYS2_ROOT_DIR "")
set(MSYS2_SUBSYSTEM "")

# ---- 仅 MinGW 环境需要 ----
if(NOT MINGW)
    return()
endif()

set(_MSYS2_SIG_SUBS "mingw64;ucrt64;clang64;mingw32;clang32")

# ---- 1. 环境变量 MSYS2_ROOT ----
if(DEFINED ENV{MSYS2_ROOT})
    file(TO_CMAKE_PATH "$ENV{MSYS2_ROOT}" MSYS2_ROOT_DIR)
    message(STATUS "[MSYS2] 从 MSYS2_ROOT 环境变量获取: ${MSYS2_ROOT_DIR}")
endif()

# ---- 2. 编译器路径反推 ----
# 例: g++ 位于 C:/msys64/ucrt64/bin/g++.exe
#     → bin → ucrt64（子系统）→ msys64（根目录）
# 同时确定根目录与子系统，避免后续重复反推。
if(NOT MSYS2_ROOT_DIR AND CMAKE_CXX_COMPILER)
    get_filename_component(_COMPILER_DIR   "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_SUBSYSTEM_DIR  "${_COMPILER_DIR}"      DIRECTORY)
    get_filename_component(_MAYBE_ROOT     "${_SUBSYSTEM_DIR}"    DIRECTORY)
    get_filename_component(_SUB_NAME       "${_SUBSYSTEM_DIR}"    NAME)

    # 校验反推得到的子系统是否在签名列表内
    foreach(_SUB ${_MSYS2_SIG_SUBS})
        if(_SUB_NAME STREQUAL _SUB AND IS_DIRECTORY "${_MAYBE_ROOT}/${_SUB}")
            set(MSYS2_ROOT_DIR  "${_MAYBE_ROOT}")
            set(MSYS2_SUBSYSTEM "${_SUB}")
            message(STATUS "[MSYS2] 从编译器路径反推: ${MSYS2_ROOT_DIR} (子系统: ${MSYS2_SUBSYSTEM})")
            break()
        endif()
    endforeach()
endif()

# 若根目录已通过环境变量得到但子系统未知，尝试从编译器路径补取子系统名
if(MSYS2_ROOT_DIR AND NOT MSYS2_SUBSYSTEM AND CMAKE_CXX_COMPILER)
    get_filename_component(_COMPILER_DIR  "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_SUBSYSTEM_DIR "${_COMPILER_DIR}"      DIRECTORY)
    get_filename_component(_SUB_NAME      "${_SUBSYSTEM_DIR}"     NAME)
    foreach(_SUB ${_MSYS2_SIG_SUBS})
        if(_SUB_NAME STREQUAL _SUB)
            set(MSYS2_SUBSYSTEM "${_SUB}")
            break()
        endif()
    endforeach()
endif()

# ---- 3. 常见安装路径扫描 ----
if(NOT MSYS2_ROOT_DIR)
    foreach(_CANDIDATE
            C:/msys64 C:/msys2
            D:/msys64 D:/msys2 "D:/Program Files/msys64"
            E:/msys64 E:/msys2)
        if(IS_DIRECTORY "${_CANDIDATE}")
            set(MSYS2_ROOT_DIR "${_CANDIDATE}")
            message(STATUS "[MSYS2] 从常见路径找到: ${MSYS2_ROOT_DIR}")
            break()
        endif()
    endforeach()
endif()

# ---- 未找到 ----
if(NOT MSYS2_ROOT_DIR)
    message(STATUS "[MSYS2] 无法定位 MSYS2 安装目录，跳过 MSYS2 探测。")
    message(STATUS "       提示: 设置环境变量 MSYS2_ROOT 指向 MSYS2 根目录")
    message(STATUS "             示例: set MSYS2_ROOT=C:\\msys64")
    return()
endif()

# ---- 构造搜索顺序：编译器子系统优先，其余按签名顺序补齐 ----
set(MSYS2_SEARCH_SUBSYSTEMS "")
if(MSYS2_SUBSYSTEM)
    list(APPEND MSYS2_SEARCH_SUBSYSTEMS "${MSYS2_SUBSYSTEM}")
endif()
foreach(_SUB ${_MSYS2_SIG_SUBS})
    if(NOT _SUB STREQUAL MSYS2_SUBSYSTEM)
        list(APPEND MSYS2_SEARCH_SUBSYSTEMS "${_SUB}")
    endif()
endforeach()

set(MSYS2_FOUND TRUE)
