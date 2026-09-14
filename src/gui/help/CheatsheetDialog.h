#pragma once

#include <QDialog>

class QComboBox;
class QTextBrowser;

/// DSL 速查手册窗口。
///
/// ## 三条刻意的设计
///
/// 1. **非模态**。看帮助的典型场景就是"照着示例改表达式"，模态窗口会让这件事做不成
///    （先前用 `open()` 是窗口级模态 —— 不阻塞事件循环，但主窗口在关闭前点不动）。
///    这里不设 `windowModality`，由 `MainWindow::showCheatsheet()` 用 `show()` 打开。
/// 2. **内容不在这里**。它来自 `batchsmith::core::dsl`（与 `bs cheatsheet` 同一份数据），
///    界面只负责显示；测试会核对两边说的是同一件事。
/// 3. **按章节渲染 HTML**，而不是一整坨纯文本：章节标题、可跳转的目录、函数表格，
///    才能让这份东西真的当"手册"用。用 `QTextBrowser` 显示自包含 HTML
///    （内容全部转义过，`$...$`、`<`、`&` 不会被当成标记）。
class CheatsheetDialog : public QDialog {
    Q_OBJECT

public:
    explicit CheatsheetDialog(QWidget* parent = nullptr);

    /// 当前显示的 HTML（供测试核对）
    [[nodiscard]] QString html() const;

    /// 纯文本快照（供测试核对"每个函数名都在里面"这类事）
    [[nodiscard]] QString visibleText() const;

    /// 目录项数（= 章节数）
    [[nodiscard]] int sectionCount() const;

    /// 跳转到第 index 章（目录下拉选中的效果，供测试驱动）
    void showSection(int index);

    /// 把手册写成 HTML 文件。
    ///
    /// 拆成静态函数是为了**可测**：真用的时候前面挂着 `QFileDialog`，而文件对话框
    /// 没法在自动化测试里驱动；真正值得验证的是"写出来的东西对不对"这一段。
    [[nodiscard]] static bool writeHtmlFile(const QString& path, QString* error);

private:
    void saveAsHtml();

    QComboBox* m_toc = nullptr;
    QTextBrowser* m_view = nullptr;
};
