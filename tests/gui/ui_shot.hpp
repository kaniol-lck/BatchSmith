#pragma once

#include <QString>

/// 把「填好示例数据的界面」渲染成一张 PNG，供人工核对版面。
///
/// 放在测试目标里而不是产品代码里，是为了不给发布产物加任何只服务于截图的开关。
/// 用法：
///
/// ```bash
/// QT_QPA_PLATFORM=offscreen BATCHSMITH_UI_SHOT=/tmp/ui.png ./batchsmith_gui_tests
/// ```
///
/// 之所以用 offscreen 平台渲染而不是抓真窗口截图：这条路确定、可重复，
/// 在 CI 上也能跑，不需要桌面会话。
///
/// @return 进程退出码（0 = 成功写出图片）
int capture_ui_shot(const QString& path);
