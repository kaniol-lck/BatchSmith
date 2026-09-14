#include "MainWindow.h"

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/version.hpp"
#include "editor/ExpressionBar.h"
#include "help/CheatsheetDialog.h"
#include "result/ResultPanel.h"
#include "table/ListSourcePanel.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("BatchSmith"));
    resize(1100, 760);

    buildCentralLayout();
    buildMenus();

    // 起始给两列，让「输入表达式 → 得到输出」这条链路立刻可试
    m_listPanel->addColumn();
    m_listPanel->addColumn();
    refreshListSummary();
}

void MainWindow::buildCentralLayout() {
    // 上半：列表区（水平滚动，N 列可增删）
    m_listPanel = new ListSourcePanel(this);

    // 下半：表达式行 + 输出列表
    m_expressionBar = new ExpressionBar(this);
    m_resultPanel = new ResultPanel(this);

    auto* bottom = new QWidget(this);
    auto* bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(8);
    bottomLayout->addWidget(m_expressionBar);
    bottomLayout->addWidget(m_resultPanel, 1);

    m_topBottomSplitter = new QSplitter(Qt::Vertical, this);
    m_topBottomSplitter->addWidget(m_listPanel);
    m_topBottomSplitter->addWidget(bottom);
    m_topBottomSplitter->setSizes({380, 340});
    m_topBottomSplitter->setChildrenCollapsible(false);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(10, 10, 10, 10);
    centralLayout->addWidget(m_topBottomSplitter);
    setCentralWidget(central);

    connect(m_expressionBar, &ExpressionBar::submitted, this, &MainWindow::evaluateExpression);
    connect(m_listPanel, &ListSourcePanel::sourcesChanged, this, &MainWindow::refreshListSummary);

    statusBar()->showMessage(QString::fromLatin1(batchsmith::core::version_banner()));
}

void MainWindow::evaluateExpression(const QString& expression) {
    using batchsmith::core::ListSourceList;
    using batchsmith::core::dsl::BatchResult;
    using batchsmith::core::dsl::evaluate_template;

    const ListSourceList sources = m_listPanel->sources();
    const BatchResult result = evaluate_template(expression, sources);

    if (!result.ok()) {
        // 算不出来时清空输出，避免旧结果留在屏幕上被当成新结果
        m_resultPanel->clear();
        m_expressionBar->showError(result.error);
        return;
    }

    m_resultPanel->setRows(result.rows);
    if (result.rows.isEmpty()) {
        m_expressionBar->showHint(QStringLiteral("计算完成：0 行（输入列表为空时该行会被跳过）"));
    } else {
        m_expressionBar->showHint(QStringLiteral("计算完成：%1 行").arg(result.rows.size()));
    }
}

void MainWindow::refreshListSummary() {
    const auto sources = m_listPanel->sources();

    QStringList parts;
    parts.reserve(sources.size());
    for (const auto& source : sources) {
        parts.append(QStringLiteral("%1(%2 项)").arg(source.name).arg(source.items.size()));
    }
    if (parts.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("还没有列表列：点「＋ 添加列表」新增"));
        return;
    }
    statusBar()->showMessage(QStringLiteral("列表源：") + parts.join(QStringLiteral("，")));
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    auto* openAction = fileMenu->addAction(QStringLiteral("打开预设(&O)…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setEnabled(false);  // 预设（TOML）属于 Phase 3
    openAction->setStatusTip(QStringLiteral("尚未实现：预设加载将在 Phase 3 接入"));

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(QStringLiteral("退出(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));

    // 语法与函数速查排在「关于」之前 —— 它是真正会被反复打开的项
    auto* cheatsheetAction = helpMenu->addAction(QStringLiteral("DSL 语法与函数速查(&K)"));
    cheatsheetAction->setObjectName(QStringLiteral("cheatsheetAction"));
    cheatsheetAction->setShortcut(QKeySequence::HelpContents);  // F1
    cheatsheetAction->setStatusTip(
            QStringLiteral("模板写法、可用的名字、全部工具函数与可运行示例"));
    connect(cheatsheetAction, &QAction::triggered, this, &MainWindow::showCheatsheet);

    helpMenu->addSeparator();

    auto* aboutAction = helpMenu->addAction(QStringLiteral("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout() {
    const QString banner = QString::fromLatin1(batchsmith::core::version_banner()).toHtmlEscaped();

    QMessageBox::about(
            this,
            QStringLiteral("关于 BatchSmith"),
            QStringLiteral("<b>BatchSmith</b> —— 把列表与表达式编译成批量操作"
                           "<br><br>%1"
                           "<br><br>表达式用 <code>$...$</code> 包裹，包裹内写 Lua；列表以"
                           " <code>list1</code>、<code>list2</code>… 注入。"
                           "<br>语法、工具函数与可运行示例：见「帮助 → DSL 语法与函数速查」（F1）。"
                           "<br>GPL-3.0")
                    .arg(banner));
}

void MainWindow::showCheatsheet() {
    // **非模态**（用 show()，不是 open() / exec()）。
    //
    // 看帮助的典型场景就是"照着示例改表达式"，模态窗口会让这件事做不成 ——
    // open() 虽然不阻塞事件循环，但它是窗口级模态：帮助开着时主窗口点不动。
    //
    // 已经开着就把它提到前面，而不是再开一个（否则按几次 F1 就会有一堆窗口）。
    if (m_cheatsheet != nullptr) {
        m_cheatsheet->show();
        m_cheatsheet->raise();
        m_cheatsheet->activateWindow();
        return;
    }

    m_cheatsheet = new CheatsheetDialog(this);
    m_cheatsheet->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_cheatsheet, &QObject::destroyed, this, [this] { m_cheatsheet = nullptr; });
    m_cheatsheet->show();
}
