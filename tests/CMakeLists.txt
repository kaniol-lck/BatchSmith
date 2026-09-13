# ---------------------------------------------------------------------------
# 单元测试
#
# 只链接 core（不碰 QtWidgets），所以能在无图形环境下运行 —— CI 上不必为
# 测试安装 X11 依赖，本机也可以直接跑。
#
# 用 doctest 而非 Qt Test：core 是纯逻辑库，不该被 Qt 的测试框架绑定；
# GUI 层的测试等 M5 再单独引入 QTest。
# ---------------------------------------------------------------------------

add_executable(batchsmith_tests
    main.cpp
    core/test_natural_compare.cpp
    core/test_version.cpp)

target_link_libraries(batchsmith_tests
    PRIVATE
        BatchSmith::Core
        batchsmith::doctest)

target_link_libraries(batchsmith_tests PRIVATE batchsmith_warnings)

add_test(NAME batchsmith.unit
         COMMAND $<TARGET_FILE:batchsmith_tests>)
