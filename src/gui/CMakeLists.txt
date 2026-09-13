# ---------------------------------------------------------------------------
# batchsmith —— 图形界面（Qt 6 Widgets）
#
# 选 Widgets 而非 QML 的依据见技术方案 ADR-3：本项目是密集数据型桌面工具，
# 没有动画/触屏/3D 需求，而 Widgets 提供最完整的 Model/View 与传统控件。
# ---------------------------------------------------------------------------

add_executable(batchsmith
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h)

target_link_libraries(batchsmith
    PRIVATE
        BatchSmith::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets)

target_link_libraries(batchsmith PRIVATE batchsmith_warnings)

# Qt 的元对象/资源/界面编译管线。现在就打开，以便 mocs 链路在 P4 就被验证，
# 而不是等到真正加信号槽时才发现问题。
set_target_properties(batchsmith PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    WIN32_EXECUTABLE ON           # Windows 下不弹控制台窗口
    MACOSX_BUNDLE ON)
