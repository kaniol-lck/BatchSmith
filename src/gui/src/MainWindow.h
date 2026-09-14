#pragma once

#include <QMainWindow>

class CheatsheetDialog;
class ExpressionBar;
class ListSourcePanel;
class ResultPanel;
class QSplitter;

/// BatchSmith 主窗口：**上下两段**。
///
/// ```text
/// ┌──────────────────────────────────────────────────────┐
/// │ 列表区（水平滚动）                                    │
/// │ ┌────────┐ ┌────────┐ ┌────────┐                     │
/// │ │ list1  │ │ list2  │ │ list3  │  ← 每列内部竖向滚动  │
/// │ │  1     │ │  a     │ │  x     │     ＋/× 控制列数量   │
/// │ │  2     │ │  b     │ │  y     │                     │
/// │ └────────┘ └────────┘ └────────┘                     │
/// ├──────────────────────────────────────────────────────┤
/// │ [ 表达式__________________________ ] [ 确定 ]         │
/// │ 提示 / 报错                                          │
/// │ 输出列表                                             │
/// │   mv 1 out                                           │
/// │   mv 2 out                                           │
/// └──────────────────────────────────────────────────────┘
/// ```
///
/// 界面本身**不含任何求值逻辑**：点的「确定」只是把当前列与表达式交给
/// `batchsmith::core::dsl::evaluate_template`，再把结果铺到输出列表上。
///
/// 这条分层约定在 Phase 2 被验证过一次：core 的求值实现从"简单求值器"换成
/// "Lua 编译器 + 沙箱"时，这个文件确实一行都不用改。后续阶段继续照此办理。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void evaluateExpression(const QString& expression);
    void refreshListSummary();
    void showAbout();
    void showCheatsheet();

private:
    void buildCentralLayout();
    void buildMenus();

    QSplitter* m_topBottomSplitter = nullptr;
    ListSourcePanel* m_listPanel = nullptr;
    ExpressionBar* m_expressionBar = nullptr;
    ResultPanel* m_resultPanel = nullptr;

    /// 帮助窗口只保持一个实例（非模态，见 showCheatsheet）
    CheatsheetDialog* m_cheatsheet = nullptr;
};
