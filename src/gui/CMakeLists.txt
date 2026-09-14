# ---------------------------------------------------------------------------
# batchsmith_gui —— 界面静态库；batchsmith —— 瘦入口可执行文件
#
# 选 Widgets 而非 QML 的依据见技术方案 ADR-3：本项目是密集数据型桌面工具，
# 没有动画/触屏/3D 需求，而 Widgets 提供最完整的 Model/View 与传统控件。
#
# 为什么界面要单独做成库：**可测**。离屏 GUI 测试（tests/gui/）需要构造
# MainWindow、按对象名找控件、模拟点击；写成「一个 add_executable 打包到底」
# 就没法把这些代码链接进测试目标。
# ---------------------------------------------------------------------------

add_library(batchsmith_gui STATIC
    src/MainWindow.cpp
    src/MainWindow.h
    editor/ExpressionBar.cpp
    editor/ExpressionBar.h
    help/CheatsheetDialog.cpp
    help/CheatsheetDialog.h
    result/ResultPanel.cpp
    result/ResultPanel.h
    table/ListSourceColumn.cpp
    table/ListSourceColumn.h
    table/ListSourcePanel.cpp
    table/ListSourcePanel.h)

add_library(BatchSmith::Gui ALIAS batchsmith_gui)

# 子目录里的头文件按 "table/xxx.h" 这种形式包含（与 docs 里的目录规划一致），
# 所以要把 src/gui 本身放进私有 include 路径；src/gui/src 则对外暴露，
# 让测试可以直接 #include "MainWindow.h"。
target_include_directories(batchsmith_gui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(batchsmith_gui
    PUBLIC
        BatchSmith::Core
        Qt6::Widgets)

target_link_libraries(batchsmith_gui PRIVATE batchsmith_warnings)

# Qt 的元对象/资源/界面编译管线。现在就打开，以便 mocs 链路在 P4 就被验证，
# 而不是等到真正加信号槽时才发现问题。
set_target_properties(batchsmith_gui PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON)

add_executable(batchsmith src/main.cpp)

target_link_libraries(batchsmith PRIVATE BatchSmith::Gui)
target_link_libraries(batchsmith PRIVATE batchsmith_warnings)

set_target_properties(batchsmith PROPERTIES
    WIN32_EXECUTABLE ON           # Windows 下不弹控制台窗口
    MACOSX_BUNDLE ON)
