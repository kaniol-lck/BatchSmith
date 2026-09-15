/// 预设菜单（保存 / 加载 / 管理）的离屏测试。
///
/// 这一组盯的是"**界面状态能存下来、也能装回去**"这条闭环：
///   * 存 → 装之后，列名、来源模式、条目、模板都一致；
///   * 文件夹路径被**槽位化**了（预设文件里不含本机绝对路径）；
///   * 未绑定的槽位要给出可读提示，而不是静默给一个空列；
///   * 窗口标题如实反映"当前预设 + 有没有未保存的改动"。
///
/// 测试隔离：入口（tests/gui/main.cpp）里设了独立的组织/应用名与 Qt 测试模式，
/// 所以这里写预设、写"最近打开"都不会污染开发机上真实的配置。

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "MainWindow.h"
#include "batchsmith/core/preset/preset.hpp"
#include "editor/ExpressionBar.h"
#include "preset/PresetManagerDialog.h"
#include "table/ListSourceColumn.h"
#include "table/ListSourcePanel.h"

#include "doctest/doctest.h"

namespace {

using batchsmith::core::ListSourceKind;
using batchsmith::core::load_preset;
using batchsmith::core::Preset;

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

void make_files(const QString& directory, const QStringList& names) {
    for (const QString& name : names) {
        QFile file(QDir(directory).filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("x") == 1);
        file.close();
    }
}

/// 往某一列填手输项（走模型，与用户在界面上编辑等价）
void set_items(MainWindow& window, const QString& columnName, const QStringList& items) {
    auto* column = window.findChild<ListSourceColumn*>(columnName);
    REQUIRE(column != nullptr);
    column->itemsView()->model()->removeRows(0, column->itemsView()->model()->rowCount());
    for (const QString& item : items) {
        const int row = column->itemsView()->model()->rowCount();
        column->itemsView()->model()->insertRow(row);
        column->itemsView()->model()->setData(column->itemsView()->model()->index(row, 0), item);
    }
}

[[nodiscard]] ListSourceColumn* column_of(MainWindow& window, const QString& name) {
    return window.findChild<ListSourceColumn*>(name);
}

/// 界面上那条提示/报错（ExpressionBar 里的 messageLabel）
[[nodiscard]] QString message_of(MainWindow& window) {
    auto* label = window.findChild<QLabel*>(QStringLiteral("messageLabel"));
    return label == nullptr ? QString() : label->text();
}

/// 每个用例都先把"最近打开"清掉：它是全局状态，会跨用例互相影响。
/// （正因为它是全局状态，才需要测试隔离把 QSettings 引到测试专用位置。）
void clear_recent() {
    QSettings settings;
    settings.remove(QStringLiteral("recentPresets"));
    settings.sync();
}

}  // namespace

TEST_CASE("预设：保存后加载，列名 / 来源模式 / 条目 / 模板都装得回来") {
    clear_recent();

    QTemporaryDir presetDir;
    QTemporaryDir folder;
    REQUIRE(presetDir.isValid());
    REQUIRE(folder.isValid());
    make_files(folder.path(),
               {S(u"番剧 第1话.mkv"), S(u"番剧 第2话.mkv"), S(u"番剧 第10话.mkv"), S(u"说明.txt")});

    const QString presetPath = QDir(presetDir.path()).filePath(S(u"往返.toml"));

    // ---- 在界面里配出一份"像那么回事"的预设 ----
    {
        MainWindow window;
        set_items(window, QStringLiteral("list1"), {S(u"01"), S(u"02"), S(u"10")});

        auto* dir_column = column_of(window, QStringLiteral("list2"));
        REQUIRE(dir_column != nullptr);
        dir_column->bindDirectory(folder.path());
        dir_column->filterEdit()->setText(S(u"*.mkv"));
        dir_column->refreshButton()->click();
        REQUIRE(dir_column->items().size() == 3);

        window.expressionBar()->setExpression(S(u"mv \"$list2[i]$\" \"第$list1[i]$话.mkv\""));

        QString error;
        REQUIRE(window.savePresetTo(presetPath, &error));
        CHECK(error.isEmpty());
        CHECK(window.presetPath() == presetPath);
        CHECK_FALSE(window.hasUnsavedChanges());
    }

    // ---- 文件里不该出现本机绝对路径（这是预设能分享的前提）----
    {
        const auto loaded = load_preset(presetPath);
        REQUIRE(loaded.ok());
        CHECK(loaded.preset.name == S(u"往返"));  // 名字取文件名
        REQUIRE(loaded.preset.lists.size() == 2);
        CHECK(loaded.preset.lists.at(0).kind == ListSourceKind::Manual);
        CHECK(loaded.preset.lists.at(1).kind == ListSourceKind::Directory);
        CHECK(loaded.preset.lists.at(1).dir.path == S(u"${input}"));

        QFile file(presetPath);
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        CHECK(text.contains(S(u"${input}")));
        CHECK_FALSE(text.contains(QDir::fromNativeSeparators(folder.path())));

        // 绑定落在伴生文件里
        QString bind_error;
        const auto bindings = batchsmith::core::load_bindings(presetPath, &bind_error);
        CHECK(bind_error.isEmpty());
        CHECK(bindings.value(S(u"input")) == QDir::fromNativeSeparators(folder.path()));
    }

    // ---- 装回来 ----
    {
        MainWindow window;
        QString error;
        REQUIRE(window.openPreset(presetPath, &error));
        CHECK(error.isEmpty());

        CHECK(window.presetPath() == presetPath);
        CHECK(window.presetName() == S(u"往返"));
        CHECK_FALSE(window.hasUnsavedChanges());
        CHECK(window.windowTitle().startsWith(S(u"往返")));
        CHECK_FALSE(window.windowTitle().contains(S(u"*")));

        // 列名与来源模式
        auto* manual = column_of(window, QStringLiteral("list1"));
        auto* directory = column_of(window, QStringLiteral("list2"));
        REQUIRE(manual != nullptr);
        REQUIRE(directory != nullptr);
        CHECK(manual->kind() == ListSourceKind::Manual);
        CHECK(directory->kind() == ListSourceKind::Directory);

        // 条目：手输的照旧，文件夹的重新读了一次（读的还是那三个 mkv）
        CHECK(manual->items() == QStringList{S(u"01"), S(u"02"), S(u"10")});
        CHECK(directory->items() ==
              QStringList{S(u"番剧 第1话.mkv"), S(u"番剧 第2话.mkv"), S(u"番剧 第10话.mkv")});
        CHECK(directory->filterEdit()->text() == S(u"*.mkv"));

        // 模板
        CHECK(window.expressionBar()->expression() ==
              S(u"mv \"$list2[i]$\" \"第$list1[i]$话.mkv\""));
    }
}

TEST_CASE("预设：未绑定的槽位要给出可读提示，而不是给一个空列") {
    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"未绑定.toml"));

    // 手写一个带槽位、但没有伴生绑定文件的预设
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '未绑定'\nversion = 1\n\n"
                   "[[lists]]\nid = 'list1'\n"
                   "source = { kind = 'dir', path = '${input}' }\n\n"
                   "[output]\ntemplate = '$list1[i]$'\n");
        file.close();
    }

    MainWindow window;
    QString error;
    // 单列读不到**不算打开失败**：预设照常打开，那一列空着并给出提示
    REQUIRE(window.openPreset(path, &error));
    CHECK(error.isEmpty());

    auto* column = column_of(window, QStringLiteral("list1"));
    REQUIRE(column != nullptr);
    CHECK(column->kind() == ListSourceKind::Directory);
    CHECK(column->items().isEmpty());

    const QString message = message_of(window);
    CHECK(message.contains(S(u"还没绑定")));
    // 提示里要带上槽位名，用户才知道要绑哪个
    CHECK(message.contains(S(u"input")));
}

TEST_CASE("预设：文件读不了时明确失败，并说清原因") {
    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());

    MainWindow window;

    const QString missing = QDir(presetDir.path()).filePath(S(u"没有这个文件.toml"));
    QString error;
    CHECK_FALSE(window.openPreset(missing, &error));
    CHECK(error.contains(S(u"打不开")));

    const QString broken = QDir(presetDir.path()).filePath(S(u"坏掉.toml"));
    QFile file(broken);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[preset\nname = 'x'\n");
    file.close();

    CHECK_FALSE(window.openPreset(broken, &error));
    CHECK(error.contains(S(u"TOML")));
}

TEST_CASE("预设：改动会让标题带上星号，保存后消失") {
    clear_recent();

    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"脏标记.toml"));

    MainWindow window;
    CHECK_FALSE(window.hasUnsavedChanges());
    CHECK(window.windowTitle().contains(S(u"未命名")));

    // 改列表内容 → 有未保存改动
    set_items(window, QStringLiteral("list1"), {S(u"a")});
    CHECK(window.hasUnsavedChanges());
    CHECK(window.windowTitle().contains(S(u"*")));

    QString error;
    REQUIRE(window.savePresetTo(path, &error));
    CHECK_FALSE(window.hasUnsavedChanges());
    CHECK_FALSE(window.windowTitle().contains(S(u"*")));
    CHECK(window.windowTitle().startsWith(S(u"脏标记")));

    // 改模板 → 同样算改动
    window.expressionBar()->setExpression(S(u"$list1[i]$"));
    CHECK(window.hasUnsavedChanges());
}

TEST_CASE("预设：加载之后立刻又算作没有改动") {
    clear_recent();

    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"干净.toml"));

    {
        MainWindow window;
        set_items(window, QStringLiteral("list1"), {S(u"x"), S(u"y")});
        window.expressionBar()->setExpression(S(u"$list1[i]$"));
        REQUIRE(window.savePresetTo(path, nullptr));
    }

    MainWindow window;
    REQUIRE(window.openPreset(path, nullptr));
    // 加载过程中"重建列"会触发内容变化信号，但收尾必须把脏标记清掉，
    // 否则用户一打开预设就被问"要不要保存"
    CHECK_FALSE(window.hasUnsavedChanges());
}

TEST_CASE("预设：新建会清空并回到未命名状态") {
    clear_recent();

    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"旧的.toml"));

    MainWindow window;
    set_items(window, QStringLiteral("list1"), {S(u"a"), S(u"b")});
    window.expressionBar()->setExpression(S(u"$list1[i]$"));
    REQUIRE(window.savePresetTo(path, nullptr));
    REQUIRE(window.openPreset(path, nullptr));

    auto* action = window.findChild<QAction*>(QStringLiteral("newPresetAction"));
    REQUIRE(action != nullptr);
    action->trigger();  // 此时没有未保存改动，不会弹确认框

    CHECK(window.presetPath().isEmpty());
    CHECK(window.windowTitle().contains(S(u"未命名")));
    CHECK(window.expressionBar()->expression().isEmpty());
    // 回到两列空列表
    CHECK(window.listPanel()->columnCount() == 2);
    CHECK(column_of(window, QStringLiteral("list1"))->items().isEmpty());
}

TEST_CASE("预设：管理对话框列出预设目录里的文件，坏文件也看得见") {
    const QString directory = batchsmith::core::default_preset_directory();
    REQUIRE(batchsmith::core::ensure_preset_directory());

    // 造两个好文件 + 一个坏文件；`.local.toml` 必须被排除（那是绑定文件）
    const QString good = QDir(directory).filePath(S(u"甲.toml"));
    const QString broken = QDir(directory).filePath(S(u"坏.toml"));
    const QString bindings = QDir(directory).filePath(S(u"甲.local.toml"));

    {
        QFile file(good);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '甲预设'\nversion = 1\n\n"
                   "[output]\ntemplate = 'x'\n");
        file.close();
    }
    {
        QFile file(broken);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("这不是 TOML\n");
        file.close();
    }
    {
        QFile file(bindings);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[bindings]\ninput = 'x'\n");
        file.close();
    }

    PresetManagerDialog dialog;
    dialog.reload();

    // 至少能看到这两个（目录里可能有其它用例留下的文件）
    QStringList names;
    QStringList tooltips;
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        const QTreeWidgetItem* item = dialog.tree()->topLevelItem(row);
        names.append(item->text(0));
        tooltips.append(item->toolTip(0));
    }
    CHECK(names.contains(S(u"甲预设")));
    // 坏文件也要列出来 —— 否则用户在"打开预设"里撞墙却找不到是哪个文件
    CHECK(names.contains(S(u"（读不了）")));
    CHECK(tooltips.join(QLatin1Char('\n')).contains(S(u"TOML")));

    // 绑定文件不是预设，不该出现
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        CHECK_FALSE(dialog.tree()->topLevelItem(row)->text(3).endsWith(S(u".local.toml")));
    }

    QFile::remove(good);
    QFile::remove(broken);
    QFile::remove(bindings);
}

TEST_CASE("预设：管理对话框选中一个才能打开，重命名与删除跟着启用") {
    const QString directory = batchsmith::core::default_preset_directory();
    REQUIRE(batchsmith::core::ensure_preset_directory());

    const QString path = QDir(directory).filePath(S(u"可选中.toml"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '可选中'\nversion = 1\n\n[output]\ntemplate = 'x'\n");
        file.close();
    }

    PresetManagerDialog dialog;
    dialog.reload();

    // 没选中时三个动作都不该可用
    CHECK_FALSE(dialog.openButton()->isEnabled());
    CHECK_FALSE(dialog.renameButton()->isEnabled());
    CHECK_FALSE(dialog.removeButton()->isEnabled());

    // 找到那一行并选中
    QTreeWidgetItem* target = nullptr;
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = dialog.tree()->topLevelItem(row);
        if (item->text(3) == S(u"可选中.toml")) {
            target = item;
            break;
        }
    }
    REQUIRE(target != nullptr);
    target->setSelected(true);

    CHECK(dialog.openButton()->isEnabled());
    CHECK(dialog.renameButton()->isEnabled());
    CHECK(dialog.removeButton()->isEnabled());

    dialog.openButton()->click();
    CHECK(dialog.selected_preset() == path);
    CHECK(dialog.result() == QDialog::Accepted);

    QFile::remove(path);
}

TEST_CASE("预设：命令行启动时直接载入（位置参数与 --preset 走同一个入口）") {
    clear_recent();

    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"启动载入.toml"));

    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '启动载入'\nversion = 1\n\n"
                   "[[lists]]\nid = 'list1'\nitems = ['甲', '乙']\n\n"
                   "[output]\ntemplate = '$list1[i]$'\n");
        file.close();
    }

    // main() 里做的就是这两步：造窗口 → openPreset(路径)
    MainWindow window;
    QString error;
    REQUIRE(window.openPreset(path, &error));
    CHECK(error.isEmpty());

    CHECK(window.presetName() == S(u"启动载入"));
    CHECK(column_of(window, QStringLiteral("list1"))->items() == QStringList{S(u"甲"), S(u"乙")});
    CHECK(window.expressionBar()->expression() == S(u"$list1[i]$"));

    // 载入失败时给出的是可读原因（main() 会把它弹出来）
    QString failure;
    CHECK_FALSE(window.openPreset(QDir(presetDir.path()).filePath(S(u"没有.toml")), &failure));
    CHECK_FALSE(failure.isEmpty());
}

TEST_CASE("预设：仓库里的示例预设能真的打开（用户第一个会试的就是它）") {
    // 刻意不断言 list1 的内容：那个槽位绑没绑取决于本机有没有 .local.toml，
    // 而这份文件在仓库里不该存在（是本机信息）。断言的只是"能打开、结构对"。
    MainWindow window;
    QString error;
    REQUIRE(window.openPreset(QStringLiteral(BATCHSMITH_SOURCE_DIR
                                             "/presets/example-rename.toml"),
                              &error));
    CHECK(error.isEmpty());

    CHECK(window.presetName() == S(u"番剧重命名"));
    CHECK(window.listPanel()->columnCount() == 2);
    CHECK_FALSE(window.hasUnsavedChanges());

    CHECK(column_of(window, QStringLiteral("list1"))->kind() == ListSourceKind::Directory);
    CHECK(column_of(window, QStringLiteral("list2"))->kind() == ListSourceKind::Manual);
    // 手输的条目写在预设文件里，所以这一条与"本机绑没绑"无关
    CHECK(column_of(window, QStringLiteral("list2"))->items().size() == 4);
    CHECK_FALSE(window.expressionBar()->expression().isEmpty());
}

TEST_CASE("预设：最近打开记住路径，并能重开") {
    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"最近.toml"));

    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '最近'\nversion = 1\n\n[output]\ntemplate = 'x'\n");
        file.close();
    }

    QSettings settings;
    settings.remove(QStringLiteral("recentPresets"));
    settings.sync();

    MainWindow window;
    REQUIRE(window.openPreset(path, nullptr));

    // 「最近打开」子菜单里应当出现它
    auto* menu = window.findChild<QMenu*>(QStringLiteral("recentPresetsMenu"));
    REQUIRE(menu != nullptr);
    REQUIRE_FALSE(menu->actions().isEmpty());
    CHECK(menu->actions().first()->text() == S(u"最近.toml"));
    CHECK(menu->actions().first()->data().toString() == path);

    // 重开一次不该出现两条
    REQUIRE(window.openPreset(path, nullptr));
    CHECK(menu->actions().size() == 1);

    settings.remove(QStringLiteral("recentPresets"));
    settings.sync();
}
