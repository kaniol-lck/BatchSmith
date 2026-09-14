cmake_minimum_required(VERSION 3.21)

project(BatchSmith
    VERSION 0.1.0
    DESCRIPTION "把列表与表达式编译成批量操作 —— 预设驱动的批处理工具"
    HOMEPAGE_URL "https://github.com/"
    LANGUAGES C CXX)

# ===========================================================================
# 全局语言设置
# ===========================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # 关掉 GNU 扩展，保证三平台语义一致

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ===========================================================================
# 可执行文件统一输出到 <build>/bin/
#
# 好处是**产物路径与生成器无关**：Ninja（单配置）与 Visual Studio（多配置，
# 默认会插一层 Release/）都会落到同一个 bin/，于是打包脚本、CI、用户手册
# 都不必关心生成器差异——这消掉了一整类"路径对不上"的失败。
# ===========================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ===========================================================================
# 禁止源码内构建
# 源码内构建会把 CMakeCache.txt 之类的垃圾丢进版本库，直接拦掉。
# ===========================================================================
if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR
        "禁止在源码目录内构建。请使用预设：\n"
        "  cmake --preset default\n"
        "或显式指定构建目录：\n"
        "  cmake -S . -B build")
endif()

# ===========================================================================
# 选项
# ===========================================================================
option(BATCHSMITH_BUILD_GUI   "构建 Qt Widgets 图形界面"   ON)
option(BATCHSMITH_BUILD_CLI   "构建命令行工具 bs"          ON)
option(BATCHSMITH_BUILD_TESTS "构建单元测试"               ON)
option(BATCHSMITH_STRICT      "把本项目代码的编译器警告当作错误" OFF)
option(BATCHSMITH_LTO         "对 Release 构建启用 LTO（体积与性能优化）" ON)

# ===========================================================================
# 编译警告策略
# 只作用于本项目自己的代码；third_party 通过不链接本 target 来豁免。
# ===========================================================================
add_library(batchsmith_warnings INTERFACE)

if(MSVC)
    target_compile_options(batchsmith_warnings INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
else()
    target_compile_options(batchsmith_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion -Wformat=2)
endif()

# ---------------------------------------------------------------------------
# 警告即错误：只在"已验证过警告集"的编译器上启用
#
# 三套编译器的警告集合并不一致。本项目逐条清理过告警的只有 GCC —— 本机
# MinGW 11.2 的 Release 构建在 -Werror 下干净通过。MSVC 的 /W4 与
# AppleClang 各有一批自己的告警，尚未逐条驯服。
#
# 所以在没验证过的编译器上**保持警告可见、但不阻断构建**。
# 这样 CI 的红色只反映真实问题，而不是"某个编译器恰好多了一条告警"；
# 告警仍然打得出来（CI 会把它们提升为注解），清干净之后再打开。
# ---------------------------------------------------------------------------
if(BATCHSMITH_STRICT)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(batchsmith_warnings INTERFACE -Werror)
    else()
        message(STATUS
            "BATCHSMITH_STRICT=ON，但编译器是 ${CMAKE_CXX_COMPILER_ID}："
            "其警告集尚未逐条驯服，暂不启用 -Werror（告警仍会打印）")
    endif()
endif()

# Release 构建的产物体积优化（对应技术方案 §5.4）
if(BATCHSMITH_LTO AND NOT MSVC)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    if(_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(STATUS "LTO 不可用：${_ipo_msg}")
    endif()
endif()

# ===========================================================================
# 依赖
# ===========================================================================
# 只依赖 QtCore 的 core 库 + 需要 Gui/Widgets 的 GUI
find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
if(BATCHSMITH_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Widgets)
endif()

# macOS 上复核上面的覆盖确实生效了。
#
# 若 cmake/FindWrapOpenGL.cmake 没被选中（例如 CMAKE_MODULE_PATH 顺序被改动），
# 症状是链接期报 "ld: framework 'AGL' not found" —— 那条信息离根因很远，
# 而且指向的是 Qt 的公开链接依赖、不是我们的代码。在这里提前断言，
# 把"十几分钟一次的 CI 往返"换成"5 秒钟的配置期错误"。
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_bs_wrapgl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_bs_wrapgl_libs AND _bs_wrapgl_libs MATCHES "AGL")
        message(FATAL_ERROR
            "WrapOpenGL::WrapOpenGL 的链接接口里仍然有 AGL：\n"
            "  ${_bs_wrapgl_libs}\n"
            "说明 cmake/FindWrapOpenGL.cmake 覆盖没有生效（CMAKE_MODULE_PATH 顺序问题），"
            "链接会以 \"ld: framework 'AGL' not found\" 失败。")
    endif()
    unset(_bs_wrapgl_libs)
endif()

# 注：core 的单测用 doctest（见 tests/CMakeLists.txt），不依赖 Qt Test。
# GUI 层的测试到 Phase 6 再单独引入 QTest。

add_subdirectory(third_party)

# ===========================================================================
# 本项目目标
# ===========================================================================
add_subdirectory(src/core)

if(BATCHSMITH_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(BATCHSMITH_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(BATCHSMITH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ===========================================================================
# 配置摘要
# ===========================================================================
message(STATUS "")
message(STATUS "==================== BatchSmith ${PROJECT_VERSION} ====================")
message(STATUS "  构建类型        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  编译器          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ 标准        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt              : ${Qt6_VERSION}  (${QT6_INSTALL_PREFIX})")
message(STATUS "  第三方 (submodule): Lua / sol2 / toml++ / doctest")
message(STATUS "  构建目标        : GUI=${BATCHSMITH_BUILD_GUI}  CLI=${BATCHSMITH_BUILD_CLI}  TESTS=${BATCHSMITH_BUILD_TESTS}")
message(STATUS "  严格警告        : ${BATCHSMITH_STRICT}")
message(STATUS "=======================================================================")
message(STATUS "")
