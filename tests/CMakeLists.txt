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
    core/test_compiler.cpp
    core/test_engine.cpp
    core/test_natural_compare.cpp
    core/test_sandbox.cpp
    core/test_template_scanner.cpp
    core/test_version.cpp)

target_link_libraries(batchsmith_tests
    PRIVATE
        BatchSmith::Core
        batchsmith::doctest)

target_link_libraries(batchsmith_tests PRIVATE batchsmith_warnings)

add_test(NAME batchsmith.unit
         COMMAND $<TARGET_FILE:batchsmith_tests>)

# ---------------------------------------------------------------------------
# GUI 层的离屏测试
#
# 单独一个 target：上面的 batchsmith_tests 刻意只链 core（无图形依赖），
# 而这个必须链 BatchSmith::Gui + Qt6::Widgets。
#
# 用 QT_QPA_PLATFORM=offscreen 跑：CI 的 Linux runner 上没有显示服务，
# 本机也不会真的弹窗口出来。
# ---------------------------------------------------------------------------
if(BATCHSMITH_BUILD_GUI)
    add_executable(batchsmith_gui_tests
        gui/main.cpp
        gui/test_main_window.cpp
        gui/ui_shot.cpp
        gui/ui_shot.hpp)

    target_link_libraries(batchsmith_gui_tests
        PRIVATE
            BatchSmith::Gui
            batchsmith::doctest)

    target_link_libraries(batchsmith_gui_tests PRIVATE batchsmith_warnings)

    add_test(NAME batchsmith.gui
             COMMAND $<TARGET_FILE:batchsmith_gui_tests>)
    set_tests_properties(batchsmith.gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
