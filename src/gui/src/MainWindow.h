#pragma once

#include <QMainWindow>

class QPlainTextEdit;
class QSplitter;
class QTableView;

/// BatchSmith 主窗口。
///
/// 当前是**骨架**：只搭出技术方案里定下的版面分区，用来验证 Qt Widgets
/// 的构建与元对象链路可用，尚未接入 core 的任何功能。
///
/// 目标版面（见构想书「界面」一节与技术方案 §7 M5）：
///
///   ┌──────────────────────────────────────────────┐
///   │ 列表区：N 个 QTableView 并排，可增删、锁定滚动 │
///   ├──────────────────────────────────────────────┤
///   │ 表达式编辑器（QPlainTextEdit + 高亮 + 补全）  │
///   ├──────────────────────────────────────────────┤
///   │ 执行结果 / Plan diff 预览                     │
///   └──────────────────────────────────────────────┘
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void showAbout();

private:
    void buildCentralLayout();
    void buildMenus();

    QSplitter* m_topBottomSplitter = nullptr;
    QSplitter* m_listSplitter = nullptr;
    QSplitter* m_editorSplitter = nullptr;
    QTableView* m_listView1 = nullptr;
    QTableView* m_listView2 = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QPlainTextEdit* m_preview = nullptr;
};
