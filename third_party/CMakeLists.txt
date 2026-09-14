# ===========================================================================
# 第三方依赖
#
# 各依赖以 git submodule 形式锁定在 third_party/README.md 记录的 tag 上，
# 仓库自身不含上游代码（只有四行 gitlink）。clone 需要 --recursive。
#
# 这些 target 不链接 batchsmith_warnings，因此本项目的严格警告策略不会
# 打进第三方代码，避免为了迁就上游写法而关掉自己的检查。
# ===========================================================================

# ---------------------------------------------------------------------------
# Lua 5.4 —— 嵌入式脚本运行时，静态链接进 core
# ---------------------------------------------------------------------------
file(GLOB _lua_all_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c")

# 必须排除的非库文件。
#
# 注意 lua/lua 的 GitHub 镜像仓库（也就是我们现在用的 submodule 来源）
# 比 lua.org 的官方发行 tarball **多带几个文件** —— ltests.c 就是随
# submodule 迁移悄悄混进来的，官方 src/ 里没有它。
#
#   lua.c    —— 官方解释器（Windows 下定义 WinMain，其它平台定义 main）
#   luac.c   —— 官方编译器（定义 main）
#   onelua.c —— 单文件合并版，它 #include 了全部源文件**并且自带解释器入口**。
#               逐文件编译时混进它，不但重复定义全部符号，还会在 Windows 下
#               把 GUI 的入口点抢走 —— 症状极具迷惑性：
#               batchsmith.exe 照常启动、退出码 0，但跑的其实是 Lua REPL。
#   ltests.c —— Lua 内部的测试模块（luaB_opentests / lua_checkmemory 等）。
#               它在静态库里是一颗"哑弹"：静态库只按需拉取目标文件，所以它
#               既不会被链进产物、也不会报错，却对外暴露一批符号，并且引用了
#               ltable.c 只在 LUA_DEBUG 下才编译的函数（luaH_getnode 等）——
#               哪天有谁把它拉进来，就是一堆未定义符号。
set(_lua_excluded_sources lua.c luac.c onelua.c ltests.c)
list(FILTER _lua_all_sources EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")

# 防御性复核：上面这条过滤一旦失效，后果是静默的（见 onelua.c 那次的教训），
# 所以逐个文件名显式断言一次，不依赖正则本身写对。
foreach(_lua_banned IN LISTS _lua_excluded_sources)
    foreach(_lua_src IN LISTS _lua_all_sources)
        get_filename_component(_lua_name "${_lua_src}" NAME)
        if(_lua_name STREQUAL _lua_banned)
            message(FATAL_ERROR
                "Lua 源列表里混入了 ${_lua_banned}。\n"
                "这些文件含可执行入口或测试代码，进库会导致入口点被抢占、"
                "或留下未定义符号。请检查上面的排除规则。")
        endif()
    endforeach()
endforeach()

add_library(lua54 STATIC ${_lua_all_sources})
add_library(batchsmith::lua ALIAS lua54)

list(LENGTH _lua_all_sources _lua_count)
message(STATUS "  Lua 编译单元    : ${_lua_count} 个源文件（已排除 lua.c / luac.c / onelua.c / ltests.c）")

target_include_directories(lua54 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lua")

set_target_properties(lua54 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# luaconf.h 在 _WIN32 下会自动定义 LUA_USE_WINDOWS，Windows 无需额外宏。
# Linux / macOS 需要显式打开，否则 liolib/loslib 会退化成最保守的实现。
if(APPLE)
    target_compile_definitions(lua54 PUBLIC LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(lua54 PUBLIC LUA_USE_LINUX)
    target_link_libraries(lua54 PUBLIC ${CMAKE_DL_LIBS})
endif()

# 关掉 MSVC 对 C 代码的"安全函数"唠叨（Lua 用标准 C 库函数）
if(MSVC)
    target_compile_definitions(lua54 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# sol2 —— Lua 的 C++ 绑定（header-only）
# ---------------------------------------------------------------------------
add_library(sol2 INTERFACE)
add_library(batchsmith::sol2 ALIAS sol2)
target_include_directories(sol2 SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/sol2/include")

# ---------------------------------------------------------------------------
# toml++ —— 预设文件解析（header-only）
# ---------------------------------------------------------------------------
add_library(tomlplusplus INTERFACE)
add_library(batchsmith::tomlplusplus ALIAS tomlplusplus)
target_include_directories(tomlplusplus SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/include")

# ---------------------------------------------------------------------------
# doctest —— 单元测试框架（header-only）
# 注意：它只进测试 target，不参与任何发布产物
#
# 上游仓库里头文件位于 doctest/doctest.h，所以 include 目录是仓库根，
# 与 sol2 / toml++ 的 include/ 布局不同。
# ---------------------------------------------------------------------------
add_library(doctest INTERFACE)
add_library(batchsmith::doctest ALIAS doctest)
target_include_directories(doctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/doctest")

# ---------------------------------------------------------------------------
# 环境自检：submodule 没初始化时给出可执行的指引，而不是让编译器报一堆
# "找不到头文件"。这是 submodule 方案最常见的翻车点。
# ---------------------------------------------------------------------------
set(_expected_files
    "${CMAKE_CURRENT_SOURCE_DIR}/lua/lua.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/sol2/include/sol/sol.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/include/toml++/toml.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/doctest/doctest/doctest.h")

foreach(_file IN LISTS _expected_files)
    if(NOT EXISTS "${_file}")
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}/.." "${_file}")
        message(FATAL_ERROR
            "第三方依赖不完整：缺少 ${_rel}\n"
            "\n"
            "third_party/ 下的依赖通过 git submodule 管理，没有随仓库提交。\n"
            "在仓库根目录执行：\n"
            "\n"
            "    git submodule update --init --recursive\n"
            "\n"
            "（.gitmodules 里已设 shallow = true，首次拉取的是浅克隆，体积很小）")
    endif()
endforeach()
