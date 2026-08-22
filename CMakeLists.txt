cmake_minimum_required(VERSION 3.15)
project(VulkanApp LANGUAGES C CXX)

# ============================================================================
# 【环境诊断】在 CMake 配置阶段自动执行，验证所有依赖是否就绪
# 检测项：CMake 版本 / 编译器 / Vulkan SDK / GLFW / GLM
# 若某项 FAIL，将给出修复建议并中止配置（FATAL_ERROR）或警告（WARNING）
# ============================================================================
include(cmake/CheckEnvironment.cmake)

# ============================================================================
# 【MinGW 导入库自动生成】若使用 MinGW 编译器，自动从 vulkan-1.dll
# 生成 libvulkan-1.dll.a，解决 Vulkan SDK 默认 MSVC .lib 不兼容问题。
# 若导入库已存在或编译器非 MinGW，则静默跳过。
# ============================================================================
include(cmake/GenerateVulkanMingwLib.cmake)

# ============================================================================
# C++ 标准
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# 查找依赖
# ============================================================================

# --- Vulkan SDK ---
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan Include: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan Library: ${Vulkan_LIBRARY}")

# --- GLFW ---
# 三层回退策略：
#   1. 标准 find_package(glfw3)  —— CMake 注册的配置文件
#   2. FindGLFW_MSYS2.cmake       —— 自动探测 MSYS2 安装的 GLFW
#   3. find_library + find_path    —— 手动指定路径（GLFW_DIR 环境变量 / 项目 lib/）
# IMPORTED 目标 glfw 的创建与 glfw3.dll 定位由共享宏完成（glfw_make_imported_target /
# glfw_locate_runtime_dll），避免与 FindGLFW_MSYS2 重复实现。
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    message(STATUS "[GLFW] find_package(glfw3) 成功")
    if(DEFINED glfw3_VERSION)
        message(STATUS "[GLFW] 版本: ${glfw3_VERSION}")
    endif()
else()
    # ---- 第 2 层：尝试 MSYS2 自动探测 ----
    message(STATUS "[GLFW] find_package(glfw3) 未找到，尝试 MSYS2 自动探测...")
    include(cmake/FindGLFW_MSYS2.cmake)
endif()

# ---- 第 3 层：通用回退（find_library + find_path）----
if(NOT GLFW_FOUND)
    message(STATUS "[GLFW] MSYS2 探测未找到，使用 find_library 回退...")

    # 同样优先静态库 .a，避免运行时依赖 glfw3.dll
    set(_GLFW_OLD_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll.a" ".lib")
    find_library(GLFW_LIBRARY
        NAMES glfw3 glfw3dll glfw
        PATHS
            "$ENV{GLFW_DIR}/lib-mingw-w64"
            "$ENV{GLFW_DIR}/lib"
            "${CMAKE_SOURCE_DIR}/lib"
        DOC "GLFW 静态/导入库路径"
    )
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_GLFW_OLD_SUFFIXES}")

    find_path(GLFW_INCLUDE_DIRS
        NAMES GLFW/glfw3.h
        PATHS
            "$ENV{GLFW_DIR}/include"
            "${CMAKE_SOURCE_DIR}/include"
        DOC "GLFW 头文件路径"
    )

    if(GLFW_LIBRARY AND GLFW_INCLUDE_DIRS)
        message(STATUS "[GLFW] find_library 找到库: ${GLFW_LIBRARY}")
        message(STATUS "[GLFW] find_path   找到头文件: ${GLFW_INCLUDE_DIRS}")
        set(GLFW_LIBRARIES "${GLFW_LIBRARY}")
        set(GLFW_FOUND TRUE)

        # 复用 FindGLFW_MSYS2 的共享宏：创建 IMPORTED 目标 + 定位运行时 DLL
        glfw_make_imported_target()
        glfw_locate_runtime_dll(GLFW_LIBRARY)
    else()
        message(FATAL_ERROR
            "GLFW 未找到。请通过以下方式之一安装 GLFW：\n"
            "  [推荐 MSYS2] pacman -S mingw-w64-ucrt-x86_64-glfw\n"
            "  [手动下载]  https://www.glfw.org/download.html\n"
            "  [环境变量]  设置 GLFW_DIR 指向 GLFW 安装目录\n"
            "  [一键部署]  运行 scripts\\setup_env.ps1")
    endif()
endif()

# --- GLM ---
# 两层回退策略：
#   1. 标准 find_package(glm)  —— CMake 注册的配置文件
#   2. FindGLM_MSYS2.cmake      —— 自动探测 MSYS2 安装的 GLM（纯头文件库）
find_package(glm QUIET)
if(glm_FOUND)
    message(STATUS "[GLM] find_package(glm) 成功")
else()
    message(STATUS "[GLM] find_package(glm) 未找到，尝试 MSYS2 自动探测...")
    include(cmake/FindGLM_MSYS2.cmake)
endif()

if(NOT GLM_FOUND)
    # 手动指定 GLM_INCLUDE_DIRS 的最后机会
    find_path(GLM_INCLUDE_DIRS_MANUAL
        NAMES glm/glm.hpp
        PATHS
            "$ENV{GLM_INCLUDE_DIR}"
            "$ENV{GLM_DIR}"
            "$ENV{GLM_ROOT}/include"
            "${CMAKE_SOURCE_DIR}/include"
        DOC "GLM 头文件路径"
    )
    if(GLM_INCLUDE_DIRS_MANUAL)
        set(GLM_INCLUDE_DIRS "${GLM_INCLUDE_DIRS_MANUAL}" CACHE PATH "GLM include directory")
        set(GLM_FOUND TRUE)
        message(STATUS "[GLM] 手动找到头文件: ${GLM_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR
            "GLM 未找到。GLM 是纯头文件库，可通过以下方式安装：\n"
            "  [推荐 MSYS2] pacman -S mingw-w64-ucrt-x86_64-glm\n"
            "  [手动下载]   https://github.com/g-truc/glm/releases\n"
            "  [环境变量]   设置 GLM_INCLUDE_DIR 指向包含 glm/glm.hpp 的目录\n"
            "  [一键部署]   运行 scripts\\setup_env.ps1")
    endif()
else()
    message(STATUS "[GLM] 包含路径: ${GLM_INCLUDE_DIRS}")
endif()

# ============================================================================
# EnTT —— ECS 库（header-only，通过 FetchContent 从 GitHub 拉取）
#
# 本项目用 EnTT 实现 ECS 架构（src/ecs/）：
#   - entt::registry 管理实体与组件
#   - 系统（System）通过 entt::view 遍历具备特定组件集合的实体
#
# 若 FetchContent 因网络问题失败，可手动操作：
#   1. 从 https://github.com/skypjack/entt/releases 下载最新 release
#   2. 解压到 third_party/entt/（使 third_party/entt/entt/ 单一包含头存在）
#   3. 设置环境变量 ENTT_DIR 指向解压目录
# ============================================================================
include(FetchContent)
set(ENTT_VERSION "v3.13.2" CACHE STRING "EnTT 版本 tag")
if(NOT ENTT_FOUND)
    # 优先使用本地 third_party/entt（手动下载）
    find_path(ENTT_INCLUDE_DIR_LOCAL
        NAMES entt/entt.hpp
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/entt"
            "$ENV{ENTT_DIR}"
        DOC "EnTT 头文件目录（含 entt/entt.hpp）"
    )
    if(ENTT_INCLUDE_DIR_LOCAL)
        set(ENTT_INCLUDE_DIRS "${ENTT_INCLUDE_DIR_LOCAL}")
        set(ENTT_FOUND TRUE)
        message(STATUS "[EnTT] 使用本地: ${ENTT_INCLUDE_DIRS}")
    else()
        # FetchContent 回退：自动从 GitHub 拉取
        message(STATUS "[EnTT] 本地未找到，使用 FetchContent 拉取（首次配置需联网）...")
        FetchContent_Declare(
            entt
            GIT_REPOSITORY https://github.com/skypjack/entt.git
            GIT_TAG        ${ENTT_VERSION}
            GIT_SHALLOW    TRUE
        )
        # 抑制 EnTT 子项目的 install 步骤
        set(ENTT_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_GetProperties(entt)
        if(NOT entt_POPULATED)
            FetchContent_Populate(entt)
        endif()
        # EnTT 是 header-only：只需拿 include 路径
        set(ENTT_INCLUDE_DIRS "${entt_SOURCE_DIR}/src" CACHE PATH "EnTT include dir")
        set(ENTT_FOUND TRUE)
        message(STATUS "[EnTT] FetchContent 完成: ${ENTT_INCLUDE_DIRS}")
    endif()
endif()

# ============================================================================
# 读取 build.json —— CMake 构建配置（exe_name / sources / extra_libraries）
# 运行时窗口配置（window_title/width/height/aspect_mode）在 config.json，
# 由源代码运行时读取，CMake 阶段只负责复制到 bin/。
# ==============================================================================
set(BUILD_CONFIG_FILE "${CMAKE_SOURCE_DIR}/build.json")
if(NOT EXISTS "${BUILD_CONFIG_FILE}")
    message(FATAL_ERROR "build.json 未找到: ${BUILD_CONFIG_FILE}")
endif()
# build.json 变更时自动重新配置
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BUILD_CONFIG_FILE}")

file(READ "${BUILD_CONFIG_FILE}" _CONFIG_JSON)
# 用正则提取 exe_name（兼容 CMake 3.15，不依赖 string(JSON) 需要 3.19+）
if(_CONFIG_JSON MATCHES "\"exe_name\"[ \t]*:[ \t]*\"([^\"]*)\"")
    set(APP_EXE_NAME "${CMAKE_MATCH_1}")
else()
    set(APP_EXE_NAME "${PROJECT_NAME}")
endif()
message(STATUS "[Build] exe_name = ${APP_EXE_NAME}")

# ============================================================================
# 从 build.json 解析 JSON 字符串数组（兼容 CMake 3.15）
# 输入：_CONFIG_JSON, 键名 → 输出变量
# ============================================================================
function(_parse_json_string_array result_var json_content key)
    # [^]] 匹配除 ] 外的任意字符（] 紧跟 [^ 表示字面 ]）
    set(_pattern "\"${key}\"[ \t]*:[ \t]*\\[([^]]*)\\]")
    if(json_content MATCHES "${_pattern}")
        set(_arr "${CMAKE_MATCH_1}")
        string(REPLACE "\"" "" _arr "${_arr}")
        string(REPLACE " " "" _arr "${_arr}")
        string(REPLACE "\n" "" _arr "${_arr}")
        string(REPLACE "\r" "" _arr "${_arr}")
        string(REPLACE "\t" "" _arr "${_arr}")
        string(REPLACE "," ";" _arr "${_arr}")
        set(_result "")
        foreach(_item ${_arr})
            if(NOT _item STREQUAL "")
                list(APPEND _result "${_item}")
            endif()
        endforeach()
        set(${result_var} "${_result}" PARENT_SCOPE)
    else()
        set(${result_var} "" PARENT_SCOPE)
    endif()
endfunction()

# ---- 解析 sources（编译源文件列表）----
_parse_json_string_array(CONFIG_SOURCES "${_CONFIG_JSON}" "sources")
if(CONFIG_SOURCES)
    # 将相对路径转为绝对路径
    set(APP_SOURCES "")
    foreach(_src ${CONFIG_SOURCES})
        list(APPEND APP_SOURCES "${CMAKE_SOURCE_DIR}/${_src}")
    endforeach()
    message(STATUS "[Build] sources = ${CONFIG_SOURCES}")
else()
    set(APP_SOURCES "")
    message(STATUS "[Build] sources 为空，回退到 GLOB_RECURSE")
endif()

# ---- 解析 extra_libraries（额外链接库）----
_parse_json_string_array(CONFIG_EXTRA_LIBS "${_CONFIG_JSON}" "extra_libraries")
if(CONFIG_EXTRA_LIBS)
    message(STATUS "[Build] extra_libraries = ${CONFIG_EXTRA_LIBS}")
else()
    message(STATUS "[Build] extra_libraries = (空)")
endif()

# ============================================================================
# 着色器编译：把 src/shaders/*.vert / *.frag 编译为 .spv
# 优先使用 Vulkan SDK 自带的 glslangValidator；找不到则尝试 glslc。
# 输出 .spv 到 ${CMAKE_BINARY_DIR}/shaders/，并在 post-build 复制到 bin/shaders。
# ============================================================================
set(SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/shaders")
set(SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/shaders")

find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32"
          "$ENV{VULKAN_SDK}/glslangValidator"
    DOC "GLSL -> SPIR-V 编译器（Vulkan SDK 自带）")

if(GLSLANG_VALIDATOR)
    message(STATUS "[Shader] 使用 glslangValidator: ${GLSLANG_VALIDATOR}")
else()
    find_program(GLSLC glslc)
    if(GLSLC)
        message(STATUS "[Shader] 使用 glslc: ${GLSLC}")
    else()
        message(WARNING
            "未找到 glslangValidator 或 glslc，无法编译着色器。"
            "请确保 Vulkan SDK 已安装并设置 VULKAN_SDK 环境变量。")
    endif()
endif()

set(SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/basic.vert"
    "${SHADER_SOURCE_DIR}/basic.frag"
    "${SHADER_SOURCE_DIR}/mesh3d.vert"
    "${SHADER_SOURCE_DIR}/mesh3d.frag"
    "${SHADER_SOURCE_DIR}/text.vert"
    "${SHADER_SOURCE_DIR}/text.frag"
    # v1.0.1 渲染引擎扩展模块着色器
    "${SHADER_SOURCE_DIR}/postfx.vert"
    "${SHADER_SOURCE_DIR}/postfx.frag"
    "${SHADER_SOURCE_DIR}/shadow.vert"
    "${SHADER_SOURCE_DIR}/shadow.frag"
    "${SHADER_SOURCE_DIR}/skybox.vert"
    "${SHADER_SOURCE_DIR}/skybox.frag"
    "${SHADER_SOURCE_DIR}/instanced.vert"
    "${SHADER_SOURCE_DIR}/instanced.frag"
    "${SHADER_SOURCE_DIR}/particle.vert"
    "${SHADER_SOURCE_DIR}/particle.frag"
    # v1.0.2 调试可视化
    "${SHADER_SOURCE_DIR}/debug.vert"
    "${SHADER_SOURCE_DIR}/debug.frag"
)

set(SHADER_TARGETS "")
foreach(SRC ${SHADER_SOURCES})
    get_filename_component(SRC_NAME ${SRC} NAME)
    get_filename_component(SRC_EXT ${SRC} LAST_EXT)
    string(REPLACE ".vert" ".vert.spv" SPV_NAME "${SRC_NAME}")
    string(REPLACE ".frag" ".frag.spv" SPV_NAME "${SPV_NAME}")
    set(SPV "${SHADER_BINARY_DIR}/${SPV_NAME}")

    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT "${SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
            COMMAND "${GLSLANG_VALIDATOR}" -V -o "${SPV}" "${SRC}"
            DEPENDS "${SRC}"
            COMMENT "[Shader] 编译 ${SRC_NAME} -> ${SPV_NAME}"
            VERBATIM
        )
    elseif(GLSLC)
        add_custom_command(
            OUTPUT "${SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
            COMMAND "${GLSLC}" -o "${SPV}" "${SRC}"
            DEPENDS "${SRC}"
            COMMENT "[Shader] 编译 ${SRC_NAME} -> ${SPV_NAME}"
            VERBATIM
        )
    endif()

    list(APPEND SHADER_TARGETS "${SPV}")
endforeach()

if(GLSLANG_VALIDATOR OR GLSLC)
    add_custom_target(compile_shaders ALL DEPENDS ${SHADER_TARGETS})
endif()

# ============================================================================
# 可执行文件
# ============================================================================
# 源文件列表由 build.json 的 sources 数组驱动；为空时回退到 GLOB_RECURSE
if(APP_SOURCES)
    set(APP_COMPILE_SOURCES ${APP_SOURCES})
else()
    file(GLOB_RECURSE APP_COMPILE_SOURCES "src/*.cpp")
endif()
add_executable(${PROJECT_NAME} ${APP_COMPILE_SOURCES})

# 着色器编译必须在可执行文件之前完成
if(TARGET compile_shaders)
    add_dependencies(${PROJECT_NAME} compile_shaders)
endif()

# 可执行文件输出到 bin/，文件名取自 build.json 的 exe_name
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    OUTPUT_NAME "${APP_EXE_NAME}"
)

# ---- post-build: 复制 config.json（运行时窗口配置）到 exe 输出目录 ----
set(RUNTIME_CONFIG_FILE "${CMAKE_SOURCE_DIR}/config.json")
if(EXISTS "${RUNTIME_CONFIG_FILE}")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RUNTIME_CONFIG_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json"
        COMMENT "[Config] 复制 config.json → $<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json"
    )
endif()

# ---- post-build: 复制编译后的 .spv 到 bin/shaders/ ----
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SHADER_BINARY_DIR}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
    COMMENT "[Shader] 复制 SPIR-V → $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
)

# ---- 若 MinGW 导入库自动生成目标存在，将其设为编译前置依赖 ----
if(TARGET generate_vulkan_mingw_lib)
    add_dependencies(${PROJECT_NAME} generate_vulkan_mingw_lib)
endif()

# ---- 若 GLFW 为动态链接，post-build 复制 glfw3.dll 到输出目录 ----
# 静态链接时此变量为空，跳过；动态链接时确保运行时能找到 DLL。
if(GLFW_RUNTIME_DLL)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GLFW_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/glfw3.dll"
        COMMENT "[GLFW] 复制运行时 DLL → $<TARGET_FILE_DIR:${PROJECT_NAME}>/glfw3.dll"
    )
endif()

# ============================================================================
# 包含目录
# ============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    ${ENTT_INCLUDE_DIRS}
)

# ============================================================================
# 链接库 —— 顺序至关重要（尤其 MinGW）
#
# 【为何 MinGW 下必须手动链接 gdi32、opengl32？】
#   GLFW 的静态库 (libglfw3.a) 在内部调用了 Win32 GDI 函数（如 ChoosePixelFormat）
#   和 OpenGL 函数；MSVC 的 .lib 文件中已记录了这些依赖（通过 #pragma comment(lib)），
#   链接器会自动解析。而 MinGW 的 GNU ld 链接器不会自动传递间接依赖，
#   必须显式列出所有被依赖的系统库，否则会出现 "undefined reference" 错误。
#   同理，链接 m（libm）提供数学函数支持。
# ============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan          # 1. Vulkan Loader
    ${GLFW_LIBRARIES}       # 2. GLFW 窗口库（使用变量兼容 MSYS2 / 手动 / find_package）
    gdi32                   # 3. Windows GDI（GLFW 静态库依赖）
    opengl32                # 4. OpenGL（GLFW 静态库依赖）
    m                       # 5. 数学库
    ${CONFIG_EXTRA_LIBS}    # 6. config.json 中 extra_libraries 指定的额外库
)

# --- 编译定义 ---
# GLFW_INCLUDE_NONE: 禁止 GLFW 自动包含 OpenGL 头文件，
# 避免与 Vulkan 的 vk_platform.h 产生类型重定义冲突（MinGW 常见问题）。
# 使用 Vulkan 时应在源码中显式 #include <vulkan/vulkan.h>。
# NOGDI / WIN32_LEAN_AND_MEAN: 防止 windows.h 把 Rectangle / Polygon 等
# GDI 函数名定义为宏，否则会与引擎中同名的图形类冲突。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    GLFW_INCLUDE_NONE
    WIN32_LEAN_AND_MEAN
    NOGDI
)

# ============================================================================
# 【注意】不再复制 vulkan-1.dll 到输出目录。
# 运行时使用 C:\Windows\System32\vulkan-1.dll（显卡驱动安装，ABI 稳定），
# 无需复制。详情见 docs/VULKAN_MINGW_SETUP.md。
# ============================================================================

# ============================================================================
# 生成 compile_commands.json（供 IntelliSense / clangd 使用）
# ============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
