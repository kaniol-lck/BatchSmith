# ---------------------------------------------------------------------------
# batchsmith_core —— 引擎静态库
#
# 只依赖 Qt6::Core（技术方案 ADR-9）：不引用 QtGui / QtWidgets，
# 于是可以在无图形环境下跑单测，也为将来换 GUI 框架留出余地。
# ---------------------------------------------------------------------------

set(BATCHSMITH_BUILD_TYPE_LABEL "${CMAKE_BUILD_TYPE}")
if(NOT BATCHSMITH_BUILD_TYPE_LABEL)
    set(BATCHSMITH_BUILD_TYPE_LABEL "MultiConfig")
endif()

# 版本头由 CMake 生成，避免版本号在两处维护
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/batchsmith/core/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/batchsmith/core/version.hpp"
    @ONLY)

add_library(batchsmith_core STATIC
    src/version.cpp
    src/dsl/cheatsheet.cpp
    src/dsl/cheatsheet_html.cpp
    src/dsl/compiler.cpp
    src/dsl/engine.cpp
    src/dsl/template_scanner.cpp
    src/helper/helpers.cpp
    src/list/dir_source.cpp
    src/list/list_source.cpp
    src/plan/plan.cpp
    src/preset/preset.cpp
    src/sandbox/sandbox.cpp
    src/text/natural_compare.cpp)

add_library(BatchSmith::Core ALIAS batchsmith_core)

target_include_directories(batchsmith_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 公共头文件不包含 Lua/sol2/toml++，所以它们是 PRIVATE 依赖：
# 不会污染使用者的 include 路径，但仍会在最终链接时被带上。
target_link_libraries(batchsmith_core
    PUBLIC
        Qt6::Core
    PRIVATE
        batchsmith::lua
        batchsmith::sol2
        batchsmith::tomlplusplus)

target_link_libraries(batchsmith_core PRIVATE batchsmith_warnings)

# 诊断开关：给沙箱的 Lua 分配器加逐块记账（见 sandbox.cpp 里的 AllocAudit）。
# 只在 CI 的专门作业里打开 —— 它对每次分配/释放都做一次哈希表查找，慢得离谱。
if(BATCHSMITH_ALLOC_AUDIT)
    target_compile_definitions(batchsmith_core PRIVATE BATCHSMITH_ALLOC_AUDIT=1)
endif()

# 诊断开关：给 Sandbox::m_violation_message 打「页面性质」追踪点（见 sandbox.cpp 的
# trace_violation_message）。用途是查 Windows Release 下 QString 数据块被提前释放的问题：
# 在「注入列表之后 / raise 赋值之后 / 拷贝之前」各看一眼，就能二分出坏在哪一步。
# 只在 Windows 上有效（用 VirtualQuery），且默认关闭 —— 它会在正常路径上多跑几次系统调用。
if(BATCHSMITH_MESSAGE_TRACE)
    target_compile_definitions(batchsmith_core PRIVATE BATCHSMITH_MESSAGE_TRACE=1)
endif()
