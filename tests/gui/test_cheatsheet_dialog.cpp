#include <doctest/doctest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextBrowser>

#include "MainWindow.h"
#include "batchsmith/core/dsl/cheatsheet.hpp"
#include "batchsmith/core/helper/helpers.hpp"
#include "help/CheatsheetDialog.h"

using batchsmith::core::dsl::cheatsheet_html;
using batchsmith::core::dsl::cheatsheet_sections;
using batchsmith::core::helper::registered_names;

// ===========================================================================
// 帮助窗口
//
// 帮助最容易出的问题不是崩溃，而是**内容与实现不符**（写了不存在的函数、
// 漏了新加的函数）。core 侧那组交叉校验挡住内容漂移，这里挡住"窗口行为"这一类：
// 是否非模态、目录能不能跳、导出的文件对不对。
// ===========================================================================

TEST_CASE("速查窗口：内容与 core 一致，并按章节渲染") {
    CheatsheetDialog dialog;

    // ⚠️ 这里**不能**断言 html() == cheatsheet_html()：html() 是
    // QTextBrowser::toHtml() 重新序列化后的结果（Qt 会加上自己的 DOCTYPE 与样式），
    // 与源串不可能逐字相等。源串的正确性由 core 侧测试负责；这里核对"关键内容都在"。
    CHECK(dialog.html().contains(QStringLiteral("<!DOCTYPE HTML")));
    CHECK(dialog.html().contains(cheatsheet_sections().first().title));
    CHECK(dialog.sectionCount() == cheatsheet_sections().size());
    CHECK_FALSE(dialog.visibleText().isEmpty());

    for (const auto& section : cheatsheet_sections()) {
        CAPTURE(section.title.toStdString());
        CHECK(dialog.visibleText().contains(section.title));
    }
    for (const QString& name : registered_names()) {
        CAPTURE(name.toStdString());
        CHECK(dialog.visibleText().contains(name + QLatin1Char('(')));
    }
}

TEST_CASE("速查窗口：显示控件可复制、不可编辑；目录项数与章节数一致") {
    CheatsheetDialog dialog;

    auto* view = dialog.findChild<QTextBrowser*>(QStringLiteral("cheatsheetView"));
    REQUIRE(view != nullptr);
    CHECK(view->isReadOnly());

    auto* toc = dialog.findChild<QComboBox*>(QStringLiteral("cheatsheetToc"));
    REQUIRE(toc != nullptr);
    CHECK(toc->count() == cheatsheet_sections().size());
}

TEST_CASE("速查窗口：目录跳转会同步选中项，越界不崩") {
    CheatsheetDialog dialog;
    auto* toc = dialog.findChild<QComboBox*>(QStringLiteral("cheatsheetToc"));
    REQUIRE(toc != nullptr);

    dialog.showSection(3);
    CHECK(toc->currentIndex() == 3);

    dialog.showSection(-1);
    dialog.showSection(999);
    CHECK(toc->currentIndex() == 3);  // 越界被忽略，状态不变
}

TEST_CASE("速查窗口：能导出成 HTML 文件；写不进去要报错而不是静默成功") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("help.html"));

    QString error;
    REQUIRE(CheatsheetDialog::writeHtmlFile(path, &error));
    CHECK(error.isEmpty());

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QString written = QString::fromUtf8(file.readAll());
    CHECK(written.contains(QStringLiteral("<!DOCTYPE html>")));
    CHECK(written.contains(cheatsheet_sections().first().title));
    for (const QString& name : registered_names()) {
        CHECK(written.contains(name + QLatin1Char('(')));
    }

    QString error2;
    const QString bad = dir.filePath(QStringLiteral("no/such/dir/x.html"));
    CHECK_FALSE(CheatsheetDialog::writeHtmlFile(bad, &error2));
    CHECK_FALSE(error2.isEmpty());
}

TEST_CASE("主窗口：帮助是**非模态**的，且重复触发只开一个窗口") {
    MainWindow window;
    window.show();

    auto* action = window.findChild<QAction*>(QStringLiteral("cheatsheetAction"));
    REQUIRE(action != nullptr);
    CHECK(action->shortcut() == QKeySequence(QKeySequence::HelpContents));

    action->trigger();
    QApplication::processEvents();

    const auto dialogs = window.findChildren<CheatsheetDialog*>();
    REQUIRE(dialogs.size() == 1);
    CHECK(dialogs.first()->isVisible());
    // 这条是用户明确要求的：帮助开着时主窗口仍要能操作，所以必须非模态
    CHECK_FALSE(dialogs.first()->isModal());

    // 再触发一次：复用同一个窗口，不该越开越多
    action->trigger();
    QApplication::processEvents();
    CHECK(window.findChildren<CheatsheetDialog*>().size() == 1);
}

TEST_CASE("主窗口：输入提示已跟上当前语法（区段里是 Lua）") {
    MainWindow window;

    auto* input = window.findChild<QLineEdit*>(QStringLiteral("expressionInput"));
    REQUIRE(input != nullptr);

    const QString placeholder = input->placeholderText();
    CHECK_FALSE(placeholder.isEmpty());
    // 提示里要出现「按 F1」与 Lua 写法的样例 —— 用户在输入框上就能找到帮助入口
    CHECK(placeholder.contains(QStringLiteral("F1")));
    CHECK(placeholder.contains(QStringLiteral("$list1[i]$")));
}
