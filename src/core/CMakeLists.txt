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
