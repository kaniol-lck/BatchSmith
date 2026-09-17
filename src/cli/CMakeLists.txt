# ---------------------------------------------------------------------------
# bs —— 命令行工具
#
# 与技术方案一致：CLI 先行。它既是自动化可复用的形态，也是"一周内是否
# 真的每天在用"这个判断门槛最省成本的验证载体。
# ---------------------------------------------------------------------------

add_executable(bs
    src/main.cpp)

# 崩溃自报只在诊断构建里进来。
#
# 刻意**不**做成一进就有的东西：它会抢在 UnhandledExceptionFilter / WER 之前把进程
# 结束掉（退出码 42），对最终用户来说等于"崩溃时不再生成系统层面的报告"。诊断时才要这个。
if(BATCHSMITH_CRASH_REPORT)
    if(NOT WIN32)
        message(FATAL_ERROR "BATCHSMITH_CRASH_REPORT 目前只在 Windows 上有实现（VEH + .map 符号化）。")
    endif()
    target_sources(bs PRIVATE src/crash_report.cpp)
    target_compile_definitions(bs PRIVATE BATCHSMITH_CRASH_REPORT=1)
endif()

target_link_libraries(bs
    PRIVATE
        BatchSmith::Core
        Qt6::Core)

target_link_libraries(bs PRIVATE batchsmith_warnings)

set_target_properties(bs PROPERTIES
    WIN32_EXECUTABLE OFF          # 保留控制台，CLI 需要 stdout
    MACOSX_BUNDLE OFF)
