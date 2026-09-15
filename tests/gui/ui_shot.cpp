#include "ui_shot.hpp"

#include <cstdio>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPixmap>
#include <QPushButton>
#include <QStringListModel>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include "MainWindow.h"
#include "batchsmith/core/preset/preset.hpp"
#include "help/CheatsheetDialog.h"
#include "preset/PresetManagerDialog.h"
#include "preset/PresetShortcut.h"
#include "table/ListSourceColumn.h"

namespace {

using batchsmith::core::ListSourceKind;

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

/// 造一个演示文件夹的内容：截图里要能看到「文件夹」这一模式的样子 ——
/// 路径、过滤、两个开关、状态行，以及自然序读入的条目。
///
/// 用真实目录而不是伪造内容：截图是给人核对界面的，摆拍的假状态没有意义。
void write_demo_files(const QString& directory) {
    const QStringList names{QStringLiteral("番剧 第1话.mkv"),
                            QStringLiteral("番剧 第2话.mkv"),
                            QStringLiteral("番剧 第9话.mkv"),
                            QStringLiteral("番剧 第10话.mkv"),
                            QStringLiteral("说明.txt")};
    for (const QString& name : names) {
        QFile file(QDir(directory).filePath(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write("x");
            file.close();
        }
    }
}

/// 把某一列切到文件夹模式并绑定（顺带填一个过滤条件，让这个功能在图上看得见）
void bind_folder(MainWindow& window, const QString& columnName, const QString& directory) {
    auto* column = window.findChild<ListSourceColumn*>(columnName);
    if (column == nullptr) {
        return;
    }
    column->bindDirectory(directory);
    column->filterEdit()->setText(QStringLiteral("*.mkv"));
    column->refreshButton()->click();
}

/// 把截图当下各列的状态打到 stderr。
///
/// 截图夹具的自证：截图是在无图形环境里生成的，生成者看不到图，看图的人
/// 又未必知道"本该有什么"。把状态打出来，重新生成时能一眼确认没截到空界面。
void report_state(MainWindow& window, const QString& path) {
    std::fprintf(stderr, "[shot] %s\n", qUtf8Printable(QDir::toNativeSeparators(path)));
    const QStringList names{QStringLiteral("list1"), QStringLiteral("list2")};
    for (const QString& name : names) {
        auto* column = window.findChild<ListSourceColumn*>(name);
        if (column == nullptr) {
            std::fprintf(stderr, "        %s = （没有这一列）\n", qUtf8Printable(name));
            continue;
        }
        std::fprintf(stderr,
                     "        %s = %s，%lld 项，状态「%s」\n",
                     qUtf8Printable(name),
                     column->kind() == ListSourceKind::Directory ? "文件夹" : "手输",
                     static_cast<long long>(column->items().size()),
                     qUtf8Printable(column->statusLabel()->text()));
    }
    if (auto* input = window.findChild<QLineEdit*>(QStringLiteral("expressionInput"))) {
        std::fprintf(stderr, "        模板 = %s\n", qUtf8Printable(input->text()));
    }
    if (auto* view = window.findChild<QListView*>(QStringLiteral("resultView"))) {
        if (auto* model = qobject_cast<QStringListModel*>(view->model())) {
            std::fprintf(stderr,
                         "        输出 = %lld 行\n",
                         static_cast<long long>(model->stringList().size()));
        }
    }
    std::fprintf(stderr,
                 "        预设 = %s%s\n",
                 qUtf8Printable(window.presetName()),
                 window.presetPath().isEmpty() ? "（还没保存过）" : "（已保存）");
}

}  // namespace

int capture_ui_shot(const QString& path) {
    MainWindow window;
    window.resize(1100, 760);

    // list1 手输、list2 绑定文件夹 —— 一张图里同时看到两种来源模式
    fillColumn(window,
               QStringLiteral("list1"),
               {QStringLiteral("01"),
                QStringLiteral("02"),
                QStringLiteral("09"),
                QStringLiteral("10")});

    // 临时目录活到函数结束，截图时目录是真实存在的
    QTemporaryDir demo(QDir::temp().filePath(QStringLiteral("BatchSmith 演示 XXXXXX")));
    if (demo.isValid()) {
        write_demo_files(demo.path());
        bind_folder(window, QStringLiteral("list2"), demo.path());
    }

    // 顺手把「输入 → 确定 → 输出列表」跑一遍，这样截图里能同时看到三处状态。
    // 模板是一个真实场景：把「番剧 第N话.mkv」改成「第NN话 正片.mkv」。
    if (auto* input = window.findChild<QLineEdit*>(QStringLiteral("expressionInput"))) {
        input->setText(QStringLiteral("mv \"$list2[i]$\" \"第$list1[i]$话 正片.mkv\""));
    }
    if (auto* confirm = window.findChild<QPushButton*>(QStringLiteral("confirmButton"))) {
        confirm->click();
    }

    // 存一次预设（写进那个临时目录，跟着一起消失）：
    // 于是截图里的标题显示的是预设名而不是「未命名*」，也顺带走过一遍保存路径 ——
    // 文件夹路径在这一步被槽位化成 ${input}，本机绑定落在旁边的 .local.toml。
    if (demo.isValid()) {
        QString save_error;
        const bool saved = window.savePresetTo(
                QDir(demo.path()).filePath(QStringLiteral("番剧重命名.toml")), &save_error);
        if (!saved) {
            std::fprintf(stderr, "[shot] 预设没存成：%s\n", qUtf8Printable(save_error));
        }
    }

    window.show();
    QApplication::processEvents();

    report_state(window, path);
    return window.grab().save(path) ? 0 : 1;
}

int capture_help_shot(const QString& path) {
    CheatsheetDialog dialog;
    dialog.resize(820, 640);
    dialog.show();
    QApplication::processEvents();
    return dialog.grab().save(path) ? 0 : 1;
}

/// 往预设目录里放几份演示预设（含一份故意写坏的），返回写进去的文件路径。
///
/// 用真实的文件而不是伪造表格行：截图是给人核对界面的，摆拍的行没有意义 ——
/// 而且"坏文件也要列出来并标红"这条只有在真有一个坏文件时才看得见。
[[nodiscard]] QStringList write_demo_presets() {
    QString error;
    if (!batchsmith::core::ensure_preset_directory(&error)) {
        std::fprintf(stderr, "[shot] 建不了预设目录：%s\n", qUtf8Printable(error));
        return {};
    }

    const QString directory = batchsmith::core::default_preset_directory();

    const struct {
        const char* file;
        const char* text;
    } demos[] = {
            {"番剧重命名.toml",
             "[preset]\nname = '番剧重命名'\nversion = 1\nnote = '只留 mkv，其余原样列出'\n\n"
             "[[lists]]\nid = 'list1'\n\n"
             "  [lists.source]\n  kind = 'dir'\n  path = '${input}'\n  filter = '*.mkv'\n\n"
             "[[lists]]\nid = 'list2'\nitems = ['01', '02', '09', '10']\n\n"
             "[output]\ntemplate = 'mv \"$list1[i]$\" \"第$list2[i]$话 正片.mkv\"'\n"},
            {"字幕批量改名.toml",
             "[preset]\nname = '字幕批量改名'\nversion = 1\nnote = 'ass/srt 一套改成 简-原名'\n\n"
             "[[lists]]\nid = 'list1'\n\n"
             "  [lists.source]\n  kind = 'dir'\n  path = '${input}'\n  filter = '*.ass|*.srt'\n\n"
             "[[lists]]\nid = 'list2'\nitems = ['简', '繁']\n\n"
             "[output]\ntemplate = 'mv \"$list1[i]$\" \"$list2[j]$-$list1[i]$'\n"},
            {"临时试的东西.toml",
             "[preset]\nname = '临时试的东西'\nversion = 1\n\n"
             "[output]\ntemplate = 'echo $list1[i]$'\n"},
            {"手改坏了.toml", "这不是合法的 TOML\n"},
    };

    QStringList written;
    for (const auto& demo : demos) {
        const QString file = QDir(directory).filePath(QString::fromUtf8(demo.file));
        QFile handle(file);
        if (handle.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            handle.write(demo.text);
            handle.close();
            written.append(file);
        }
    }
    return written;
}

int capture_preset_shot(const QString& path) {
    const QStringList written = write_demo_presets();

    PresetManagerDialog dialog;
    dialog.resize(960, 520);
    dialog.reload();

    // 选中第一行：这样「创建快捷方式」在图上呈现"可用"的样子，
    // 否则截图里那个按钮永远是灰的，看不出它是能点的
    if (auto* first = dialog.tree()->topLevelItem(0)) {
        first->setSelected(true);
    }

    dialog.show();
    QApplication::processEvents();

    // 自证输出：截图是在无图形环境里生成的，生成者看不到图
    int rows = 0;
    int unreadable = 0;
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* item = dialog.tree()->topLevelItem(row);
        if (item->text(0) == QStringLiteral("（读不了）")) {
            ++unreadable;
        }
        ++rows;
    }
    std::fprintf(stderr, "[shot] %s\n", qUtf8Printable(QDir::toNativeSeparators(path)));
    std::fprintf(
            stderr,
            "        预设目录 = %s\n"
            "        列表 %d 行（其中读不了 %d 行）\n"
            "        创建快捷方式按钮 = %s（扩展名 %s）\n",
            qUtf8Printable(QDir::toNativeSeparators(batchsmith::core::default_preset_directory())),
            rows,
            unreadable,
            dialog.shortcutButton()->isEnabled() ? "可用" : "不可用",
            qUtf8Printable(shortcut_suffix()));

    const bool saved = dialog.grab().save(path);

    for (const QString& file : written) {
        QFile::remove(file);
    }
    return saved ? 0 : 1;
}
