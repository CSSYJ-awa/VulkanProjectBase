# ==============================================================================
# CheckEnvironment.cmake —— 开发环境完整性诊断模块
#
# 调用方式：在 CMakeLists.txt 中 project() 之后添加
#   include(cmake/CheckEnvironment.cmake)
#
# 检测项：
#   0. CMake 版本        >= 3.15
#   1. C++ 编译器         GNU (MinGW) 且版本 >= 8.0
#   2. Vulkan SDK         环境变量 + 头文件 + 导入库 + 系统 DLL
#   3. GLFW               库文件 + 头文件（标准 find_package + MSYS2 探测）
#   4. GLM                纯头文件库（标准 find_package + MSYS2 探测）
#
# 【设计说明】
#   · 本模块仅做"诊断报告"：调用 FindGLFW_MSYS2 / FindGLM_MSYS2 完成实际探测，
#     不重复实现查找逻辑，避免诊断与实际查找双套代码出现不一致。
#   · 查找模块产生的 IMPORTED 目标 / 变量供主 CMakeLists.txt 直接复用。
# ==============================================================================

# ---- 辅助宏：打印状态行 / 分隔线 / 计数 ----
macro(check_print_status label result detail)
    if(${result})
        message(STATUS "  ${label}  [OK]    ${detail}")
    else()
        message(STATUS "  ${label}  [FAIL]  ${detail}")
    endif()
endmacro()

macro(check_print_separator)
    message(STATUS "  --------------------------------------------------")
endmacro()

set(CHECK_PASS_COUNT 0)
set(CHECK_FAIL_COUNT 0)

macro(check_record result)
    if(${result})
        math(EXPR CHECK_PASS_COUNT "${CHECK_PASS_COUNT} + 1")
    else()
        math(EXPR CHECK_FAIL_COUNT "${CHECK_FAIL_COUNT} + 1")
    endif()
endmacro()

# ============================================================================
# 阶段 0：CMake 自身版本
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║       VulkanApp 开发环境诊断 (CMake)                ║")
message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")

check_print_separator()
message(STATUS "  [0] CMake 版本")
check_print_separator()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.15")
    check_print_status("CMake 版本 >= 3.15" TRUE "CMake ${CMAKE_VERSION}")
    check_record(TRUE)
else()
    check_print_status("CMake 版本 >= 3.15" FALSE "当前 ${CMAKE_VERSION}，请升级至 3.15+")
    check_record(FALSE)
    message(FATAL_ERROR "CMake 版本过低 (${CMAKE_VERSION})，需要 >= 3.15。"
                        "请从 https://cmake.org/download/ 下载最新版本。")
endif()

# ============================================================================
# 阶段 1：C++ 编译器
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [1] C++ 编译器")
check_print_separator()

set(COMPILER_OK TRUE)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    check_print_status("编译器类型 (GNU)" TRUE "${CMAKE_CXX_COMPILER_ID}")
else()
    check_print_status("编译器类型 (GNU)" FALSE
        "当前: ${CMAKE_CXX_COMPILER_ID}。强烈建议使用 MinGW-w64 (GNU)。")
    set(COMPILER_OK FALSE)
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "8.0")
    check_print_status("编译器版本 >= 8.0" TRUE "GCC ${CMAKE_CXX_COMPILER_VERSION}")
else()
    check_print_status("编译器版本 >= 8.0" FALSE
        "当前 GCC ${CMAKE_CXX_COMPILER_VERSION}，C++17 需要 >= 8.0")
    set(COMPILER_OK FALSE)
endif()

check_print_status("编译器路径" TRUE "${CMAKE_CXX_COMPILER}")

check_record(${COMPILER_OK})
if(NOT COMPILER_OK)
    message(FATAL_ERROR "编译器不满足要求。请安装 MinGW-w64 (GCC >= 8.0)：\n"
                        "  https://www.mingw-w64.org/\n"
                        "  或 TDM-GCC: https://jmeubank.github.io/tdm-gcc/")
endif()

# ============================================================================
# 阶段 2：Vulkan SDK
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [2] Vulkan SDK")
check_print_separator()

set(VULKAN_OK TRUE)

# 将环境变量转换为 CMake 正斜杠路径（避免反斜杠转义错误）
set(VULKAN_SDK_ROOT "$ENV{VULKAN_SDK}")
if(VULKAN_SDK_ROOT)
    file(TO_CMAKE_PATH "${VULKAN_SDK_ROOT}" VULKAN_SDK_ROOT)
endif()

if(VULKAN_SDK_ROOT)
    check_print_status("VULKAN_SDK 环境变量" TRUE "${VULKAN_SDK_ROOT}")
else()
    check_print_status("VULKAN_SDK 环境变量" FALSE
        "未设置。请从 https://vulkan.lunarg.com/ 下载安装 Vulkan SDK")
    set(VULKAN_OK FALSE)
endif()

# 头文件
if(VULKAN_SDK_ROOT)
    set(VULKAN_HEADER "${VULKAN_SDK_ROOT}/Include/vulkan/vulkan.h")
    if(EXISTS "${VULKAN_HEADER}")
        check_print_status("vulkan/vulkan.h 头文件" TRUE "${VULKAN_HEADER}")
    else()
        check_print_status("vulkan/vulkan.h 头文件" FALSE "缺少: ${VULKAN_HEADER}")
        set(VULKAN_OK FALSE)
    endif()
endif()

# 导入库：MinGW 用 libvulkan-1.dll.a，MSVC 用 vulkan-1.lib
if(VULKAN_SDK_ROOT)
    set(VULKAN_LIB_MINGW   "${VULKAN_SDK_ROOT}/Lib/libvulkan-1.dll.a")
    set(VULKAN_LIB_MSVC    "${VULKAN_SDK_ROOT}/Lib/vulkan-1.lib")
    set(VULKAN_LIB_STATIC  "${VULKAN_SDK_ROOT}/Lib/libvulkan.a")

    if(EXISTS "${VULKAN_LIB_MINGW}")
        file(SIZE "${VULKAN_LIB_MINGW}" _LIB_SIZE)
        check_print_status("Vulkan 导入库 (MinGW)" TRUE
            "${VULKAN_LIB_MINGW} (${_LIB_SIZE} bytes)")
    elseif(EXISTS "${VULKAN_LIB_STATIC}")
        check_print_status("Vulkan 静态库" TRUE "${VULKAN_LIB_STATIC}")
    elseif(EXISTS "${VULKAN_LIB_MSVC}" AND NOT MINGW)
        check_print_status("Vulkan 导入库 (MSVC)" TRUE "${VULKAN_LIB_MSVC}")
    elseif(EXISTS "${VULKAN_LIB_MSVC}" AND MINGW)
        # MinGW 下仅有 MSVC .lib 不可用，但 CMake 会自动生成（GenerateVulkanMingwLib）
        check_print_status("Vulkan 导入库 (MinGW)" FALSE
            "仅有 MSVC vulkan-1.lib，MinGW 无法链接 → CMake 将自动生成")
    else()
        check_print_status("Vulkan 导入库" FALSE "缺少 MinGW/MSVC 导入库")
        set(VULKAN_OK FALSE)
    endif()
endif()

# 运行时 DLL（系统级，由显卡驱动或 Vulkan Runtime 安装）
set(VULKAN_SYSTEM_DLL "C:/Windows/System32/vulkan-1.dll")
if(EXISTS "${VULKAN_SYSTEM_DLL}")
    check_print_status("vulkan-1.dll (System32)" TRUE "${VULKAN_SYSTEM_DLL}")
else()
    check_print_status("vulkan-1.dll (System32)" FALSE
        "缺少: ${VULKAN_SYSTEM_DLL}，请安装显卡驱动或 Vulkan Runtime")
    set(VULKAN_OK FALSE)
endif()

check_record(${VULKAN_OK})
if(NOT VULKAN_OK)
    message(FATAL_ERROR "Vulkan SDK 检测失败。\n"
                        "  1. 下载 Vulkan SDK: https://vulkan.lunarg.com/\n"
                        "  2. 安装后确保 VULKAN_SDK 环境变量已设置\n"
                        "  3. 如手动设置，请指向 SDK 根目录，例如 C:\\VulkanSDK\\1.3.296.0")
endif()

# ============================================================================
# 阶段 3：GLFW —— 直接调用查找模块（不重复实现探测逻辑）
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [3] GLFW 窗口库")
check_print_separator()

# 先尝试标准 find_package（glfw3 / glfw 两种包名）
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    check_print_status("find_package(glfw3)" TRUE "已找到")
    if(DEFINED glfw3_VERSION)
        check_print_status("GLFW 版本" TRUE "${glfw3_VERSION}")
    endif()
    set(GLFW_OK TRUE)
else()
    find_package(glfw QUIET)
    if(glfw_FOUND)
        check_print_status("find_package(glfw)" TRUE "已找到 (包名: glfw)")
        set(GLFW_OK TRUE)
    else()
        # 标准 find_package 失败 → 调用 FindGLFW_MSYS2 完成实际查找
        # 该模块会创建 IMPORTED 目标 glfw，供主 CMakeLists.txt 复用
        include(cmake/FindGLFW_MSYS2.cmake)
        if(GLFW_FOUND)
            check_print_status("GLFW (MSYS2 探测)" TRUE "${GLFW_LIBRARY}")
            set(GLFW_OK TRUE)
        else()
            check_print_status("GLFW" FALSE "标准 find_package 与 MSYS2 探测均未找到")
            set(GLFW_OK FALSE)
        endif()
    endif()
endif()

check_record(${GLFW_OK})
if(NOT GLFW_OK)
    message(STATUS "             修复建议:")
    message(STATUS "               · MSYS2: pacman -S mingw-w64-ucrt-x86_64-glfw")
    message(STATUS "               · 手动下载: https://www.glfw.org/download.html")
    message(STATUS "               · 一键部署: scripts\\setup_env.ps1")
endif()

# ============================================================================
# 阶段 4：GLM —— 直接调用查找模块
# ============================================================================
message(STATUS "")
check_print_separator()
message(STATUS "  [4] GLM 数学库 (纯头文件)")
check_print_separator()

find_package(glm QUIET)
if(glm_FOUND)
    check_print_status("find_package(glm)" TRUE "已找到")
    set(GLM_OK TRUE)
else()
    # 标准配置未找到 → 调用 FindGLM_MSYS2 完成实际查找
    include(cmake/FindGLM_MSYS2.cmake)
    if(GLM_FOUND)
        check_print_status("GLM (MSYS2 探测)" TRUE "${GLM_INCLUDE_DIRS}/glm/glm.hpp")
        set(GLM_OK TRUE)
    else()
        check_print_status("GLM" FALSE "标准 find_package 与 MSYS2 探测均未找到")
        set(GLM_OK FALSE)
    endif()
endif()

check_record(${GLM_OK})
if(NOT GLM_OK)
    message(STATUS "             修复建议:")
    message(STATUS "               · MSYS2: pacman -S mingw-w64-ucrt-x86_64-glm")
    message(STATUS "               · 手动下载: https://github.com/g-truc/glm/releases")
    message(STATUS "               · 一键部署: scripts\\setup_env.ps1")
endif()

# ============================================================================
# 汇总报告
# ============================================================================
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════╗")
message(STATUS "║              环境诊断结果汇总                        ║")
message(STATUS "╠══════════════════════════════════════════════════════╣")

math(EXPR CHECK_TOTAL "${CHECK_PASS_COUNT} + ${CHECK_FAIL_COUNT}")
message(STATUS "║  总计检测 ${CHECK_TOTAL} 项")
message(STATUS "║  通过: ${CHECK_PASS_COUNT}  [OK]")

if(CHECK_FAIL_COUNT GREATER 0)
    message(STATUS "║  失败: ${CHECK_FAIL_COUNT}  [FAIL]  ← 请根据上述建议修复")
    message(STATUS "╠══════════════════════════════════════════════════════╣")
    message(STATUS "║  部分检测未通过，构建可能失败。                      ║")
else()
    message(STATUS "║  失败: 0")
    message(STATUS "╠══════════════════════════════════════════════════════╣")
    message(STATUS "║  所有检测通过！环境就绪，可以构建。                  ║")
endif()

message(STATUS "╚══════════════════════════════════════════════════════╝")
message(STATUS "")
