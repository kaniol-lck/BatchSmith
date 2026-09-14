#include "ui_shot.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QPushButton>
#include <QStringListModel>
#include <QWidget>

#include "MainWindow.h"
#include "help/CheatsheetDialog.h"

namespace {

/// 往某一列填项（列的对象名就是列表名，见 ListSourceColumn 构造函数）
void fillColumn(MainWindow& window, const QString& columnName, const QStringList& items) {
    auto* column = window.findChild<QWidget*>(columnName);
    if (column == nullptr) {
        return;
    }
    auto* view = column->findChild<QListView*>(QStringLiteral("itemsView"));
    if (view == nullptr) {
        return;
    }
    if (auto* model = qobject_cast<QStringListModel*>(view->model())) {
        model->setStringList(items);
    }
}

}  // namespace

int capture_ui_shot(const QString& path) {
    MainWindow window;
    window.resize(1100, 760);

    // 示例数据取自构想书的「使用示例」，让截图能直接和文档对着看
    fillColumn(window,
               QStringLiteral("list1"),
               {QStringLiteral("报告A"), QStringLiteral("报告B"), QStringLiteral("报告C")});
    fillColumn(window,
               QStringLiteral("list2"),
               {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")});

    // 顺手把「输入 → 确定 → 输出列表」跑一遍，这样截图里能同时看到三处状态
    if (auto* input = window.findChild<QLineEdit*>(QStringLiteral("expressionInput"))) {
        // 用行索引写法：模板读到 i ⇒ 逐行对应（0.3.0 起的推荐写法）
        input->setText(QStringLiteral("$list1[i]$-第$list2$版"));
    }
    if (auto* confirm = window.findChild<QPushButton*>(QStringLiteral("confirmButton"))) {
        confirm->click();
    }

    window.show();
    QApplication::processEvents();

    return window.grab().save(path) ? 0 : 1;
}

int capture_help_shot(const QString& path) {
    CheatsheetDialog dialog;
    dialog.resize(820, 640);
    dialog.show();
    QApplication::processEvents();
    return dialog.grab().save(path) ? 0 : 1;
}
