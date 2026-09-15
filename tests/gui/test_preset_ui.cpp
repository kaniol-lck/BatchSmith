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
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QProcess>
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
#include "preset/PresetShortcut.h"
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
        names.append(item->text(PresetManagerDialog::ColumnPreset));
        tooltips.append(item->toolTip(PresetManagerDialog::ColumnPreset));
    }
    CHECK(names.contains(S(u"甲预设")));
    // 坏文件也要列出来 —— 否则用户在"打开预设"里撞墙却找不到是哪个文件
    CHECK(names.contains(S(u"（读不了）")));
    CHECK(tooltips.join(QLatin1Char('\n')).contains(S(u"TOML")));

    // 绑定文件不是预设，不该出现
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        CHECK_FALSE(dialog.tree()
                            ->topLevelItem(row)
                            ->text(PresetManagerDialog::ColumnFile)
                            .endsWith(S(u".local.toml")));
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
        if (item->text(PresetManagerDialog::ColumnFile) == S(u"可选中.toml")) {
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
    REQUIRE(window.openPreset(QStringLiteral(BATCHSMITH_SOURCE_DIR "/presets/example-rename.toml"),
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

TEST_CASE("预设：菜单里直接列出最近用过的，当前那个带勾") {
    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString first = QDir(presetDir.path()).filePath(S(u"最近.toml"));
    const QString second = QDir(presetDir.path()).filePath(S(u"另一套.toml"));

    for (const QString& path : {first, second}) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = 'x'\nversion = 1\n\n[output]\ntemplate = 'x'\n");
        file.close();
    }

    clear_recent();

    MainWindow window;
    auto* menu = window.findChild<QMenu*>(QStringLiteral("presetMenu"));
    REQUIRE(menu != nullptr);

    // 一个都没用过时给一句"怎么才能有"，而不是一个一片空白的菜单
    REQUIRE(menu->actions().size() >= 2);
    CHECK(menu->actions().first()->text().contains(S(u"还没有预设")));
    CHECK_FALSE(menu->actions().first()->isEnabled());

    // 最近用过的**直接列在菜单里**（不套子菜单）：这个菜单存在的唯一理由是
    // "点一下就切过去"，多一层就白做了
    const auto recent_actions = [menu] {
        QList<QAction*> found;
        for (QAction* action : menu->actions()) {
            if (action->objectName() == QLatin1String("presetMenuRecent")) {
                found.append(action);
            }
        }
        return found;
    };

    REQUIRE(window.openPreset(first, nullptr));
    REQUIRE(recent_actions().size() == 1);
    CHECK(recent_actions().first()->text() == S(u"最近.toml"));
    CHECK(recent_actions().first()->data().toString() == first);
    // 当前打开的那个带勾 —— 菜单里一眼看得出"我现在在哪一套里"
    CHECK(recent_actions().first()->isChecked());

    // 换另一套：它排到最前，勾跟着走
    REQUIRE(window.openPreset(second, nullptr));
    REQUIRE(recent_actions().size() == 2);
    CHECK(recent_actions().at(0)->text() == S(u"另一套.toml"));
    CHECK(recent_actions().at(0)->isChecked());
    CHECK_FALSE(recent_actions().at(1)->isChecked());

    // 重开同一个不该出现两条
    REQUIRE(window.openPreset(first, nullptr));
    CHECK(recent_actions().size() == 2);

    // ---- 走真正的入口：点菜单项 ----
    // 这条路径必须单独测：点它会触发"重建菜单"，而 QMenu::clear() 会 delete
    // 正在发射 triggered 的那个 action。直接调 openPreset() 是测不到的。
    // 此刻顺序是 [最近, 另一套]，点第二项就是切到「另一套」
    REQUIRE(recent_actions().at(0)->isChecked());
    REQUIRE(recent_actions().at(1)->text() == S(u"另一套.toml"));
    recent_actions().at(1)->trigger();
    QCoreApplication::processEvents();  // 打开被推到了事件循环下一轮

    CHECK(window.presetPath() == second);
    CHECK(recent_actions().size() == 2);  // 切过去不该多出一条
    CHECK(recent_actions().at(0)->text() == S(u"另一套.toml"));
    CHECK(recent_actions().at(0)->isChecked());
    CHECK_FALSE(recent_actions().at(1)->isChecked());

    // 文件被外部删掉后不该还留在菜单里 —— 留着点了必然失败，比空着更烦
    REQUIRE(QFile::remove(second));
    REQUIRE(window.openPreset(first, nullptr));  // 打开会重建菜单
    CHECK(recent_actions().size() == 1);
    CHECK(recent_actions().first()->text() == S(u"最近.toml"));

    clear_recent();
}

TEST_CASE("预设：保存不问路径，直接落进预设目录") {
    clear_recent();

    const QString directory = batchsmith::core::default_preset_directory();
    REQUIRE(batchsmith::core::ensure_preset_directory());
    // 记下已有的文件，跑完只删自己造的那些 —— 这个目录是跨用例共享的
    const QStringList before = QDir(directory).entryList(QDir::Files);

    {
        MainWindow window;
        set_items(window, QStringLiteral("list1"), {S(u"a"), S(u"b")});
        window.expressionBar()->setExpression(S(u"$list1[i]$"));

        auto* save = window.findChild<QAction*>(QStringLiteral("savePresetAction"));
        REQUIRE(save != nullptr);
        save->trigger();  // 关键：**不弹文件对话框**，这一行能直接跑完

        CHECK_FALSE(window.hasUnsavedChanges());
        CHECK_FALSE(window.presetPath().isEmpty());
        CHECK(QFileInfo(window.presetPath()).absolutePath() ==
              QFileInfo(directory).absoluteFilePath());

        // 真的落盘了，而且读得回来
        const auto loaded = load_preset(window.presetPath());
        REQUIRE(loaded.ok());
        CHECK(loaded.preset.template_text == S(u"$list1[i]$"));

        // 再存一次不该另开一个文件（路径已经定了）
        const QString path = window.presetPath();
        set_items(window, QStringLiteral("list1"), {S(u"a"), S(u"b"), S(u"c")});
        CHECK(window.hasUnsavedChanges());
        save->trigger();
        CHECK(window.presetPath() == path);
        CHECK_FALSE(window.hasUnsavedChanges());
        QFile::remove(path);
    }

    {
        // 第二个新建的窗口：默认文件名重了要加序号，不能把上一份盖掉
        MainWindow window;
        set_items(window, QStringLiteral("list1"), {S(u"z")});
        auto* save = window.findChild<QAction*>(QStringLiteral("savePresetAction"));
        REQUIRE(save != nullptr);
        save->trigger();

        const QString path = window.presetPath();
        CHECK_FALSE(path.isEmpty());
        QFile::remove(path);
    }

    for (const QString& name : QDir(directory).entryList(QDir::Files)) {
        if (!before.contains(name)) {
            QFile::remove(QDir(directory).filePath(name));
        }
    }
}

TEST_CASE("预设：管理对话框显示输出表达式与备注；备注进预设文件、图标只进本机设置") {
    const QString directory = batchsmith::core::default_preset_directory();
    REQUIRE(batchsmith::core::ensure_preset_directory());

    QTemporaryDir assets;
    REQUIRE(assets.isValid());

    const QString path = QDir(directory).filePath(S(u"表达式与备注.toml"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '表达式与备注'\nversion = 1\n\n"
                   "[[lists]]\nid = 'list1'\nitems = ['a']\n\n"
                   "[output]\ntemplate = 'mv $list1[i]$ 正片'\n");
        file.close();
    }

    // 一个**真的**图片文件：`QIcon` 只认得出真图片，随便塞几个字节进去的话
    // `setIcon` 拿到的是空图标，界面上什么都看不到（测试也就测不出东西）
    const QString icon = QDir(assets.path()).filePath(S(u"番剧.png"));
    {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor(0x30, 0x80, 0xc0));
        REQUIRE(image.save(icon));
    }

    PresetManagerDialog dialog;
    dialog.reload();

    // 按文件列找那一行；每次改动后对话框会重建列表，所以行指针不能留着用
    const auto find_row = [&dialog, &path]() -> QTreeWidgetItem* {
        for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
            QTreeWidgetItem* item = dialog.tree()->topLevelItem(row);
            if (item->data(PresetManagerDialog::ColumnPreset, Qt::UserRole).toString() == path) {
                return item;
            }
        }
        return nullptr;
    };

    QTreeWidgetItem* row = find_row();
    REQUIRE(row != nullptr);
    // 输出表达式直接摊在一列里 —— "这个预设到底会做什么"最直接的答案
    CHECK(row->text(PresetManagerDialog::ColumnTemplate) == S(u"mv $list1[i]$ 正片"));
    CHECK(row->text(PresetManagerDialog::ColumnNote) == S(u"—"));  // 还没写备注

    // ---- 备注：写进预设文件 ----
    QString error;
    REQUIRE(dialog.setNote(path, S(u"给番剧用，只留 mkv"), &error));
    CHECK(error.isEmpty());
    CHECK(load_preset(path).preset.note == S(u"给番剧用，只留 mkv"));

    row = find_row();
    REQUIRE(row != nullptr);
    CHECK(row->text(PresetManagerDialog::ColumnNote) == S(u"给番剧用，只留 mkv"));
    CHECK(row->toolTip(PresetManagerDialog::ColumnNote) == S(u"给番剧用，只留 mkv"));

    // 备注在预设文件里 —— 它会跟着预设一起分享出去（这是刻意的）
    QFile preset_file(path);
    REQUIRE(preset_file.open(QIODevice::ReadOnly));
    CHECK(QString::fromUtf8(preset_file.readAll()).contains(S(u"给番剧用，只留 mkv")));
    preset_file.close();

    // ---- 图标：进本机设置，**不进**预设文件 ----
    CHECK_FALSE(dialog.clearIconButton()->isEnabled());  // 还没设过
    REQUIRE(dialog.setShortcutIcon(path, icon, &error));
    CHECK(batchsmith::core::load_local_settings(path).shortcut_icon == icon);

    preset_file.open(QIODevice::ReadOnly);
    // 图标是本机路径：预设文件里**不该**出现它，否则"发给别人"就带上了本机信息
    CHECK_FALSE(QString::fromUtf8(preset_file.readAll()).contains(icon));
    preset_file.close();

    row = find_row();
    REQUIRE(row != nullptr);
    CHECK_FALSE(row->icon(PresetManagerDialog::ColumnPreset).isNull());  // 名字旁边画出来了

    dialog.tree()->clearSelection();
    row = find_row();
    REQUIRE(row != nullptr);
    row->setSelected(true);
    CHECK(dialog.clearIconButton()->isEnabled());

    // ---- 清除图标：本机设置里没了，绑定之类不受影响 ----
    REQUIRE(dialog.setShortcutIcon(path, QString(), &error));
    CHECK(batchsmith::core::load_local_settings(path).shortcut_icon.isEmpty());

    QFile::remove(path);
    QFile::remove(batchsmith::core::bindings_path_for(path));
}

TEST_CASE("预设：管理对话框能给预设建快捷方式（建到指定目录）") {
    const QString directory = batchsmith::core::default_preset_directory();
    REQUIRE(batchsmith::core::ensure_preset_directory());

    QTemporaryDir shortcutDir;
    REQUIRE(shortcutDir.isValid());

    const QString path = QDir(directory).filePath(S(u"建快捷方式.toml"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '建快捷方式'\nversion = 1\n\n[output]\ntemplate = 'x'\n");
        file.close();
    }

    PresetManagerDialog dialog;
    // 不往开发机真实桌面上放东西
    dialog.setShortcutDirectory(shortcutDir.path());
    dialog.reload();

    // 先设一个图标：它应当被带到快捷方式上
    QTemporaryDir assets;
    REQUIRE(assets.isValid());
    const QString icon = QDir(assets.path()).filePath(S(u"icon.png"));
    {
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(QColor(0x30, 0x80, 0xc0));
        REQUIRE(image.save(icon));
    }
    QString icon_error;
    REQUIRE(dialog.setShortcutIcon(path, icon, &icon_error));

    // 没选中时不能建（与打开 / 重命名一致）
    CHECK_FALSE(dialog.shortcutButton()->isEnabled());

    QTreeWidgetItem* target = nullptr;
    for (int row = 0; row < dialog.tree()->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = dialog.tree()->topLevelItem(row);
        if (item->text(PresetManagerDialog::ColumnFile) == S(u"建快捷方式.toml")) {
            target = item;
            break;
        }
    }
    REQUIRE(target != nullptr);
    target->setSelected(true);
    CHECK(dialog.shortcutButton()->isEnabled());

    dialog.shortcutButton()->click();

    // 关键：建出来的东西**真的能用** —— 指向本程序的 exe，带着这个预设的绝对路径
    const QFileInfoList created = QDir(shortcutDir.path()).entryInfoList(QDir::Files, QDir::Name);
    REQUIRE(created.size() == 1);
    CHECK(created.first().completeBaseName() == S(u"建快捷方式"));

    ShortcutTarget shortcut;
    QString error;
    REQUIRE_MESSAGE(read_shortcut(created.first().absoluteFilePath(), &shortcut, &error),
                    error.toStdString());
    CHECK(shortcut.program == QCoreApplication::applicationFilePath());
    CHECK(shortcut.preset_path() == QFileInfo(path).absoluteFilePath());
#if !defined(Q_OS_MACOS)
    // 预设设了图标，建的快捷方式就该用它（macOS 的 .command 没有图标位置）
    CHECK(shortcut.icon == icon);
#endif

    // 成功**不弹框**（连着建几个时不该每建一个点一次"确定"），结果写在状态行里
    CHECK(dialog.statusLabel()->text().contains(created.first().fileName()));
    CHECK(dialog.statusLabel()->text().contains(S(u"建快捷方式")));

    QFile::remove(path);
}

TEST_CASE("预设：真正的可执行文件接受 --preset 启动参数") {
    // 「创建快捷方式」生成的就是 `exe --preset "<文件>"`，所以"这个命令行能用"
    // 是那条功能的**全部价值所在**。上面那条用例测的是 `MainWindow::openPreset()`
    // （= main() 里该做的那两步），不是 main() 自己 —— 参数解析一旦坏掉，
    // 快捷方式双击就只会开出一个空窗口，而且没有任何东西会报警。
    //
    // 这里只断言"没有当场死掉"：窗口程序要用户关掉才退出，拿不到有意义的退出码。
    // （因此"弹了个错误框卡在那儿"也会算通过 —— 这是这条用例的已知边界。）
    const QString binDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
            QDir(binDir).filePath(QStringLiteral("batchsmith.exe")),
            QDir(binDir).filePath(QStringLiteral("batchsmith")),
            // macOS 下是可执行文件在 bundle 里面
            QDir(binDir).filePath(QStringLiteral("batchsmith.app/Contents/MacOS/batchsmith"))};

    QString exe;
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) {
            exe = candidate;
            break;
        }
    }
    if (exe.isEmpty()) {
        // 只构建了测试目标、或平台布局不同时跳过，而不是假装通过
        WARN("找不到 batchsmith 可执行文件（两个目标不在同一个输出目录？），跳过");
        return;
    }

    QTemporaryDir presetDir;
    REQUIRE(presetDir.isValid());
    const QString path = QDir(presetDir.path()).filePath(S(u"命令行.toml"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("[preset]\nname = '命令行'\nversion = 1\n\n[output]\ntemplate = 'x'\n");
        file.close();
    }

    // ⚠️ 这一条会在**真实**配置里留下一条"最近用过"的记录（子进程没有测试模式）。
    // 那个路径随后随临时目录消失，下次读取时会被自动丢掉 ——
    // 为了覆盖真正的入口，这个代价可以接受。
    QProcess process;
    process.start(exe, {QStringLiteral("--preset"), path});
    REQUIRE(process.waitForStarted());
    // false = 还在跑 = 没当场死掉
    CHECK_FALSE(process.waitForFinished(1500));
    process.kill();
    process.waitForFinished();
}
