# ---------------------------------------------------------------------------
# bs —— 命令行工具
#
# 与技术方案一致：CLI 先行。它既是自动化可复用的形态，也是"一周内是否
# 真的每天在用"这个判断门槛最省成本的验证载体。
# ---------------------------------------------------------------------------

add_executable(bs
    src/main.cpp)

target_link_libraries(bs
    PRIVATE
        BatchSmith::Core
        Qt6::Core)

target_link_libraries(bs PRIVATE batchsmith_warnings)

set_target_properties(bs PROPERTIES
    WIN32_EXECUTABLE OFF          # 保留控制台，CLI 需要 stdout
    MACOSX_BUNDLE OFF)
