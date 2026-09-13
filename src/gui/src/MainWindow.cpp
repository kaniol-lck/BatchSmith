#include "MainWindow.h"

#include <QAction>
#include <QHeaderView>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>

#include "batchsmith/core/version.hpp"

namespace {

/// 骨架阶段的示例表格。
///
/// 用 QStandardItemModel 填几行示例值，是为了让版面分区一眼可见、便于核对
/// "多列表并排"这个交互；**不是**真实的数据模型。M5 会换成
/// QAbstractTableModel 子类，直接绑定 core 的列表模型（含缺省方式与自然排序）。
QTableView* makePlaceholderList(const QStringList& sample_values) {
    auto* model = new QStandardItemModel;
    model->setColumnCount(1);
    model->setHorizontalHeaderLabels({QStringLiteral("值")});
    for (const QString& value : sample_values) {
        model->appendRow(new QStandardItem(value));
    }

    auto* view = new QTableView;
    view->setModel(model);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->setVisible(false);
    return view;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("BatchSmith"));
    resize(1100, 720);

    buildCentralLayout();
    buildMenus();

    statusBar()->showMessage(QString::fromLatin1(batchsmith::core::version_banner()));
}

void MainWindow::buildCentralLayout() {
    // 示例值取自构想书「使用示例」一节，方便直接把界面和文档对着看
    m_listView1 =
            makePlaceholderList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")});
    m_listView2 =
            makePlaceholderList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

    // 多列表并排。锁定滚动后续通过同步各视图的 QScrollBar 位置实现（ADR-3）。
    m_listSplitter = new QSplitter(Qt::Horizontal);
    m_listSplitter->addWidget(m_listView1);
    m_listSplitter->addWidget(m_listView2);
    m_listSplitter->setSizes({500, 500});

    // 表达式编辑器：留空并给提示，形态就是最终要用的 QPlainTextEdit。
    // M5 接上 QSyntaxHighlighter（DSL 高亮）与 QCompleter（helper / 列表名补全）。
    m_editor = new QPlainTextEdit;
    m_editor->setPlaceholderText(
            QStringLiteral("在此写输出表达式，例如：\nmv $list1[i]$ $list2[i]$\n\n"
                           "（高亮与补全尚未接入）"));

    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setPlaceholderText(
            QStringLiteral("输出预览 / Plan diff\n\n"
                           "执行前在此展示可核对的改动，未显式确认不产生任何副作用。"));

    m_editorSplitter = new QSplitter(Qt::Horizontal);
    m_editorSplitter->addWidget(m_editor);
    m_editorSplitter->addWidget(m_preview);
    m_editorSplitter->setSizes({500, 500});

    m_topBottomSplitter = new QSplitter(Qt::Vertical);
    m_topBottomSplitter->addWidget(m_listSplitter);
    m_topBottomSplitter->addWidget(m_editorSplitter);
    m_topBottomSplitter->setSizes({340, 320});

    setCentralWidget(m_topBottomSplitter);
}

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    auto* openAction = fileMenu->addAction(QStringLiteral("打开预设(&O)…"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setEnabled(false);  // M2 接入 TOML 预设后再启用
    openAction->setStatusTip(QStringLiteral("尚未实现：将在 M2 接入预设加载"));

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(QStringLiteral("退出(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto* aboutAction = helpMenu->addAction(QStringLiteral("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout() {
    const QString banner = QString::fromLatin1(batchsmith::core::version_banner()).toHtmlEscaped();

    QMessageBox::about(this,
                       QStringLiteral("关于 BatchSmith"),
                       QStringLiteral("<b>BatchSmith</b> —— 把列表与表达式编译成批量操作"
                                      "<br><br>%1"
                                      "<br><br>当前为工程骨架，功能尚未接入。"
                                      "<br>GPL-3.0")
                               .arg(banner));
}
