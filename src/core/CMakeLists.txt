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

# ⚠️ 运行时开关（run #49）：`BATCHSMITH_HOLD` 那份"多持一份引用"**本身就是遮罩**
#   （它把引用计数抬高一份，正好抵消"多减一次"）。要证明这句话，必须能在**同一份二进制**里
#   把它关掉再看一遍 —— 换构建去比就分不清"运行时效应"与"编译期效应"（run #38/#39 的教训）。
#   所以 `sandbox.cpp` 里的 `hold_message_reference()` 额外认一个环境变量：
#       BATCHSMITH_MESSAGE_NO_HOLD=1  ⇒  探针照打，但不持有那个块
#   它**不需要**编译期开关（只在 MESSAGE_TRACE 构建里有这段代码），所以这里没有对应的
#   CMake 选项 —— 记在这里是为了让"这些环境变量在哪读的"有据可查。

# 诊断 A/B 开关（run #48）：把「指令数中止」那条消息从**内联临时对象**改成**具名局部**。
#
# 为什么值得单独做一个开关：run #47 的 ④h（全页堆 + CLI）已经确定崩溃是
# **use-after-free** —— `engine.cpp:169` 那次**合法**拷贝对 `m_violation_message` 的
# 数据块做引用计数自增，而那个块**早就被释放了**（`0xc0000005` 写在
# `Qt6Core.dll+0x10860` 的 `lock xadd` 上）。缺的那一环是"谁多减了一次"，
# 而它只能发生在「`raise()` 返回 → `lua_pcall` 返回」这段窗口里。
#
# 这段窗口里唯一和消息块有关的 C++ 对象，就是 `limit_hook` 里那个**内联临时**：
#     sandbox->raise(Violation::Instructions,
#                    QStringLiteral("指令数超过上限（%1）").arg(limit));
# 临时对象绑定到 `const QString&` 参数，寿命到**整条完整表达式结束**为止；
# 而具名局部的寿命到**块结束**，而 `luaL_error` 是 `longjmp` —— 具名局部根本来不及析构。
# ⇒ 两种写法的引用计数收支**不一样**，正好可以当单变量 A/B：
#    * 这个开关一开就"好了" ⇒ 问题就出在那个临时对象的生命周期上；
#    * 开了照样在同一条指令上 `0xc0000005` ⇒ 与临时无关，凶手在 Lua 错误传播那条路上。
#
# ⚠️ 它**不持有也不观察**那个数据块（只是换个存储位置），所以不属于 run #47 查明的
# 「in-core 仪器本身就是遮罩」那一类 —— 可以放心做对照。
if(BATCHSMITH_ABORT_MSG_LOCAL)
    target_compile_definitions(batchsmith_core PRIVATE BATCHSMITH_ABORT_MSG_LOCAL=1)
endif()

# 诊断 A/B 的下一档（run #49）：V4 + 显式 `clear()` 一次。
#
# 为什么需要它：V4 之所以"绿"，有两种**完全不同**的微观解释，而它们对"下一步该改哪里"
# 指向相反的方向：
#   ① 那条内联临时**被析构了两次**（真的多减一次）⇒ 元凶就是它；
#   ② 具名局部只是**少减一次**（`longjmp` 跳过析构）⇒ V4 是"把引用计数抬高一份"遮出来的，
#      元凶在别处。
# 单看 V4 分不开：两种解释都预测"V4 绿"。所以再加一档 —— **把少减的那一次补回来**
# （`QString::clear()` = `*this = QString()`，对块恰好减一次），让引用计数回到
# 与"内联临时正常析构一次"**完全相同**的状态，再看它还崩不崩：
#   * V5 仍 5/5 ⇒ 同一状态、V0 却崩 ⇒ 解释 ①；
#   * V5 变回 0/5 ⇒ 解释 ②。
#
# ⚠️ 判据**先写死**（在 ci.yml 与 CHANGELOG 里各存一份），跑完再解释就会自己骗自己。
# ⚠️ 与 `…_LOCAL` 一样：它**不持有也不观察**那个块（只多一次合法的析构等价物），
#   不属于「in-core 仪器本身就是遮罩」那一类。
if(BATCHSMITH_ABORT_MSG_LOCAL_CLEAR)
    target_compile_definitions(batchsmith_core PRIVATE BATCHSMITH_ABORT_MSG_LOCAL_CLEAR=1)
endif()
