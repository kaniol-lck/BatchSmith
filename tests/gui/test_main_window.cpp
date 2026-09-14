#include <doctest/doctest.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QStringListModel>
#include <QToolButton>
#include <QWidget>

#include "MainWindow.h"

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 把主窗口包一层，顺手按对象名把要用的控件都找出来。
///
/// 测试**刻意从对象名去找控件**（而不是给 MainWindow 加一堆 getter）：
/// 这样界面内部怎么组织不影响测试，而对象名本来也是 Qt 里调试与自动化该有的东西。
struct Ui {
    MainWindow window;
    QLineEdit* input = nullptr;
    QPushButton* confirm = nullptr;
    QListView* result = nullptr;
    QLabel* message = nullptr;
    QToolButton* addColumn = nullptr;

    Ui() {
        input = window.findChild<QLineEdit*>(QStringLiteral("expressionInput"));
        confirm = window.findChild<QPushButton*>(QStringLiteral("confirmButton"));
        result = window.findChild<QListView*>(QStringLiteral("resultView"));
        message = window.findChild<QLabel*>(QStringLiteral("messageLabel"));
        addColumn = window.findChild<QToolButton*>(QStringLiteral("addColumnButton"));

        REQUIRE(input != nullptr);
        REQUIRE(confirm != nullptr);
        REQUIRE(result != nullptr);
        REQUIRE(message != nullptr);
        REQUIRE(addColumn != nullptr);
    }

    /// 某一列内部的列表视图（列的对象名就是列表名）
    [[nodiscard]] QListView* itemsView(const QString& columnName) const {
        auto* column = window.findChild<QWidget*>(columnName);
        return column == nullptr ? nullptr
                                 : column->findChild<QListView*>(QStringLiteral("itemsView"));
    }

    /// 直接往某一列填项 —— 等价于用户手输，但不依赖编辑器控件的细节
    void setItems(const QString& columnName, const QStringList& items) {
        QListView* view = itemsView(columnName);
        REQUIRE(view != nullptr);
        auto* model = qobject_cast<QStringListModel*>(view->model());
        REQUIRE(model != nullptr);
        model->setStringList(items);
    }

    /// 点某列的 ×
    void removeColumn(const QString& columnName) {
        auto* column = window.findChild<QWidget*>(columnName);
        REQUIRE(column != nullptr);
        auto* button = column->findChild<QToolButton*>(QStringLiteral("removeColumnButton"));
        REQUIRE(button != nullptr);
        button->click();
        processDeferredDeletes();
    }

    /// Qt 里删控件走 deleteLater()（在控件自己的槽函数里立刻 delete 自己是不安全的，
    /// 而「×」的槽正好在被删的那个控件里）。删除因此被排到事件循环的
    /// DeferredDelete 队列上，离屏测试没有事件循环在跑，必须手动送一次 ——
    /// 这不是迁就实现，而是把真实的消息循环补上，否则断言看到的是"还没死"的控件。
    void processDeferredDeletes() const {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    [[nodiscard]] QStringList resultRows() const {
        QStringList rows;
        for (int row = 0; row < result->model()->rowCount(); ++row) {
            rows.append(result->model()->index(row, 0).data().toString());
        }
        return rows;
    }

    void submit(const QString& expression) {
        input->setText(expression);
        confirm->click();
    }

    void submitByEnter(const QString& expression) {
        input->setText(expression);
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(input, &press);
    }
};

}  // namespace

TEST_CASE("初始状态：两列，名为 list1 与 list2") {
    const Ui ui;
    CHECK(ui.itemsView(S(u"list1")) != nullptr);
    CHECK(ui.itemsView(S(u"list2")) != nullptr);
    CHECK(ui.window.findChild<QWidget*>(S(u"list3")) == nullptr);
}

TEST_CASE("列可以增减，编号取当前未占用的最小编号") {
    Ui ui;

    ui.addColumn->click();
    CHECK(ui.itemsView(S(u"list3")) != nullptr);  // 追加得到 list3

    // 删掉 list1 之后，剩下的列不改名 —— 表达式里的引用不会静默换目标
    ui.removeColumn(S(u"list1"));
    CHECK(ui.itemsView(S(u"list1")) == nullptr);
    CHECK(ui.itemsView(S(u"list2")) != nullptr);
    CHECK(ui.itemsView(S(u"list3")) != nullptr);

    // 再新增时优先补回空出来的编号
    ui.addColumn->click();
    CHECK(ui.itemsView(S(u"list1")) != nullptr);
}

TEST_CASE("点「确定」把表达式算成输出列表") {
    Ui ui;
    ui.setItems(S(u"list1"), {S(u"1"), S(u"2"), S(u"3")});

    ui.submit(S(u"$list1$"));
    CHECK(ui.resultRows() == QStringList{S(u"1"), S(u"2"), S(u"3")});
    CHECK(ui.message->text().contains(S(u"3 行")));
}

TEST_CASE("字面段随展开广播（mv 例子）") {
    Ui ui;
    ui.setItems(S(u"list1"), {S(u"a"), S(u"b")});

    ui.submit(S(u"mv $list1$ out"));
    CHECK(ui.resultRows() == QStringList{S(u"mv a out"), S(u"mv b out")});
}

TEST_CASE("两个列表相乘取笛卡尔积，右侧为内层循环") {
    Ui ui;
    ui.setItems(S(u"list1"), {S(u"1"), S(u"2"), S(u"3")});
    ui.setItems(S(u"list2"), {S(u"a"), S(u"b"), S(u"c")});

    ui.submit(S(u"$list1$-$list2$"));
    const QStringList rows = ui.resultRows();
    REQUIRE(rows.size() == 9);
    CHECK(rows.first() == S(u"1-a"));
    CHECK(rows.at(1) == S(u"1-b"));
    CHECK(rows.at(3) == S(u"2-a"));
    CHECK(rows.last() == S(u"3-c"));
}

TEST_CASE("回车与点「确定」等价") {
    Ui ui;
    ui.setItems(S(u"list1"), {S(u"x"), S(u"y")});

    ui.submitByEnter(S(u"$list1$"));
    CHECK(ui.resultRows() == QStringList{S(u"x"), S(u"y")});
}

TEST_CASE("空表达式时「确定」不可点") {
    Ui ui;
    CHECK_FALSE(ui.confirm->isEnabled());
    ui.input->setText(S(u"$list1$"));
    CHECK(ui.confirm->isEnabled());
}

TEST_CASE("求值报错时清空输出，不把旧结果留在屏幕上") {
    Ui ui;
    ui.setItems(S(u"list1"), {S(u"1"), S(u"2")});

    ui.submit(S(u"$list1$"));
    REQUIRE(ui.resultRows().size() == 2);

    ui.submit(S(u"$list9$"));  // 不存在的列表
    CHECK(ui.resultRows().isEmpty());
    CHECK(ui.message->text().contains(S(u"list9")));
}
