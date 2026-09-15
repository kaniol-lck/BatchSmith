#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/version.hpp"
#include "editor/ExpressionBar.h"
#include "help/CheatsheetDialog.h"
#include "preset/PresetManagerDialog.h"
#include "result/ResultPanel.h"
#include "table/ListSourcePanel.h"

namespace {

using batchsmith::core::ListSource;
using batchsmith::core::ListSourceList;
using batchsmith::core::Preset;
using batchsmith::core::preset_from_sources;
using batchsmith::core::preset_slots;
using batchsmith::core::preset_sources;
using batchsmith::core::PresetLoad;
using batchsmith::core::SlotBindings;

/// 最近用过的预设最多记几条。
///
/// 「预设」菜单里它们**直接列出来**（不套子菜单），所以这个数字就是那个菜单的
/// 前几行 —— 8 条还在"一眼扫得完"的范围内。
constexpr int kMaxRecentPresets = 8;
const char* const kRecentKey = "recentPresets";

/// 还没保存过的预设，默认叫什么（文件名会按 `未命名预设 2.toml` 这样去重）
const char* const kUntitledPresetName = "未命名预设";

/// 预设名 = 文件名去掉扩展名。
///
/// 规则单一，且与 `load_preset()` 里"文件没写 name 时用文件名兜底"完全一致：
/// 文件叫什么，预设就叫什么 —— 不给"两个名字不一致"留出解释空间。
[[nodiscard]] QString name_from_path(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    buildCentralLayout();
    buildMenus();

    // 起始给两列，让「输入表达式 → 得到输出」这条链路立刻可试
    m_listPanel->addColumn();
    m_listPanel->addColumn();
    refreshListSummary();

    // 上面两步会触发 sourcesChanged（那是"内容变了"），但刚开出来的窗口
    // 不该显示"有未保存改动"
    setDirty(false);
    updateWindowTitle();
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

    // 内容变化 = 预设内容变化。加载预设时中途也会走到这里，但 openPreset 收尾时
    // 会统一 setDirty(false)，所以不需要额外的"正在加载"标志。
    connect(m_listPanel, &ListSourcePanel::sourcesChanged, this, [this] { setDirty(true); });
    connect(m_expressionBar, &ExpressionBar::textEdited, this, [this] { setDirty(true); });

    statusBar()->showMessage(QString::fromLatin1(batchsmith::core::version_banner()));
}

void MainWindow::evaluateExpression(const QString& expression) {
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

// ---------------------------------------------------------------------------
// 菜单
// ---------------------------------------------------------------------------

void MainWindow::buildMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    auto* newAction = fileMenu->addAction(QStringLiteral("新建预设(&N)"));
    newAction->setObjectName(QStringLiteral("newPresetAction"));
    newAction->setShortcut(QKeySequence::New);
    newAction->setStatusTip(QStringLiteral("清空列表与表达式，重新开始"));
    connect(newAction, &QAction::triggered, this, &MainWindow::newPreset);

    auto* openAction = fileMenu->addAction(QStringLiteral("打开预设(&O)…"));
    openAction->setObjectName(QStringLiteral("openPresetAction"));
    openAction->setShortcut(QKeySequence::Open);
    openAction->setStatusTip(QStringLiteral("从 .toml 预设文件载入列表与表达式"));
    connect(openAction, &QAction::triggered, this, &MainWindow::choosePresetToOpen);

    fileMenu->addSeparator();

    m_saveAction = fileMenu->addAction(QStringLiteral("保存预设(&S)"));
    m_saveAction->setObjectName(QStringLiteral("savePresetAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(
            QStringLiteral("保存到当前预设文件；还没存过时会存进预设目录（要挑地方用「另存为」）"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::savePreset);

    auto* saveAsAction = fileMenu->addAction(QStringLiteral("另存为(&A)…"));
    saveAsAction->setObjectName(QStringLiteral("savePresetAsAction"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction->setStatusTip(QStringLiteral("存到别的地方，或另起一个名字（之后默认存到那里）"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::savePresetAs);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction(QStringLiteral("退出(&Q)"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // ---------------------------------------------------------------------
    // 「预设」菜单：**换预设**用的，不是"文件操作"菜单
    //
    // 分开的理由：`文件 → 保存` 是每个程序都有的惯例位置，用户找它时是往「文件」
    // 看的；而"我常用的那几套配置"是像书签一样的东西，越用越长。混在一个菜单里，
    // 长起来的那一列会把"保存"挤到看不见的地方。
    // ---------------------------------------------------------------------
    m_presetMenu = menuBar()->addMenu(QStringLiteral("预设(&P)"));
    m_presetMenu->setObjectName(QStringLiteral("presetMenu"));
    rebuildPresetMenu();
    // 预设文件可能被用户在别处删掉或改名，每次弹出前重扫一遍
    connect(m_presetMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildPresetMenu);

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

// ---------------------------------------------------------------------------
// 预设：状态
// ---------------------------------------------------------------------------

void MainWindow::setDirty(bool dirty) {
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    QString name = QStringLiteral("未命名");
    if (!m_presetPath.isEmpty()) {
        name = m_preset.name.isEmpty() ? name_from_path(m_presetPath) : m_preset.name;
    }
    setWindowTitle(QStringLiteral("%1%2 — BatchSmith")
                           .arg(name, m_dirty ? QStringLiteral("*") : QString()));
}

bool MainWindow::confirmDiscardChanges() {
    if (!m_dirty) {
        return true;
    }

    const QString name =
            m_presetPath.isEmpty() ? QStringLiteral("未命名") : QFileInfo(m_presetPath).fileName();
    const auto answer =
            QMessageBox::warning(this,
                                 QStringLiteral("BatchSmith"),
                                 QStringLiteral("「%1」有未保存的改动。").arg(name),
                                 QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                 QMessageBox::Save);

    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        savePreset();
        return !m_dirty;  // 保存失败（或用户取消了保存对话框）就不继续
    }
    return true;
}

// ---------------------------------------------------------------------------
// 预设：新建 / 打开
// ---------------------------------------------------------------------------

void MainWindow::newPreset() {
    if (!confirmDiscardChanges()) {
        return;
    }

    // 两列空列表 —— 与刚启动时的状态一致
    ListSourceList sources;
    sources.append(ListSource{QStringLiteral("list1"), {}});
    sources.append(ListSource{QStringLiteral("list2"), {}});
    m_listPanel->setSources(sources);
    m_expressionBar->setExpression(QString());
    m_resultPanel->clear();

    m_preset = Preset{};
    m_presetPath.clear();
    setDirty(false);
    updateWindowTitle();
    refreshListSummary();
}

void MainWindow::choosePresetToOpen() {
    const QString start = m_presetPath.isEmpty() ? batchsmith::core::default_preset_directory()
                                                 : QFileInfo(m_presetPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("打开预设"),
                                                      start,
                                                      QStringLiteral("预设文件 (*.toml);;"
                                                                     "所有文件 (*)"));
    if (path.isEmpty()) {
        return;  // 用户取消
    }
    openPresetFromUi(path);
}

/// 打开一个预设：先问未保存的改动，再打开，失败时把原因说清楚。
///
/// 菜单项、最近列表、文件对话框三条入口都走它 —— 否则"从最近列表打开"那条路
/// 很容易漏掉某一步（开始时就是漏了失败提示）。
void MainWindow::openPresetFromUi(const QString& path) {
    if (!confirmDiscardChanges()) {
        return;
    }
    QString error;
    if (!openPreset(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开预设失败"), error);
    }
}

bool MainWindow::openPreset(const QString& path, QString* error) {
    const PresetLoad loaded = batchsmith::core::load_preset(path);
    if (!loaded.ok()) {
        if (error != nullptr) {
            *error = loaded.error;
        }
        return false;
    }

    // 绑定文件坏了不该让预设打不开 —— 那只是"本机路径记不住"，重绑一次就有
    QString bind_error;
    const SlotBindings bindings = batchsmith::core::load_bindings(path, &bind_error);

    QHash<QString, QString> scan_errors;
    const ListSourceList sources = preset_sources(loaded.preset, bindings, &scan_errors);

    m_listPanel->setSources(sources);
    m_expressionBar->setExpression(loaded.preset.template_text);
    m_resultPanel->clear();

    m_preset = loaded.preset;
    m_presetPath = path;
    setDirty(false);  // 上面几步会触发"内容变了"，这里统一收尾
    updateWindowTitle();
    rememberRecent(path);
    refreshListSummary();

    // ---- 把"有什么没能恢复"说清楚 ----
    QStringList unbound;
    for (const QString& slot : preset_slots(loaded.preset)) {
        if (!bindings.contains(slot)) {
            unbound.append(slot);
        }
    }

    QStringList notes;
    if (!bind_error.isEmpty()) {
        notes.append(bind_error);
    }
    if (!unbound.isEmpty()) {
        notes.append(QStringLiteral("有 %1 个路径槽位还没绑定：%2 —— "
                                    "把文件夹拖到对应列上就会自动绑好（保存时记住）")
                             .arg(unbound.size())
                             .arg(unbound.join(QStringLiteral("、"))));
    }
    for (auto it = scan_errors.constBegin(); it != scan_errors.constEnd(); ++it) {
        notes.append(QStringLiteral("%1：%2").arg(it.key(), it.value()));
    }

    if (!notes.isEmpty()) {
        const QString text = notes.join(QLatin1Char('\n'));
        m_expressionBar->showError(text);
        statusBar()->showMessage(text, 10000);
    }

    return true;
}

// ---------------------------------------------------------------------------
// 预设：保存
// ---------------------------------------------------------------------------

void MainWindow::savePreset() {
    if (!m_presetPath.isEmpty()) {
        savePresetTo(m_presetPath, nullptr);
        return;
    }

    // 从没存过（刚启动 / 刚「新建」）：**不问路径**，直接落到预设目录。
    //
    // 点「保存」时想的是"存起来别丢"，而不是"我要挑个地方"。预设目录是这个程序
    // 自己的地方，存那儿下次一定找得到（「预设」菜单里就列着）。要挑地方有
    // 「另存为」—— 那才是明确表达了"我要放到别处"的动作。
    QString error;
    if (!batchsmith::core::ensure_preset_directory(&error)) {
        QMessageBox::warning(this, QStringLiteral("保存预设"), error);
        return;
    }

    const QString base =
            m_preset.name.isEmpty() ? QString::fromLatin1(kUntitledPresetName) : m_preset.name;
    savePresetTo(batchsmith::core::unique_file_path(batchsmith::core::default_preset_directory(),
                                                    base,
                                                    QStringLiteral(".toml"),
                                                    QString::fromLatin1(kUntitledPresetName)),
                 nullptr);
}

void MainWindow::savePresetAs() {
    QString error;
    if (!batchsmith::core::ensure_preset_directory(&error)) {
        QMessageBox::warning(this, QStringLiteral("保存预设"), error);
        return;
    }

    // 默认名字就是当前预设名：另存为最常见的用途是"在现有基础上改个变体"，
    // 起名时有个像样的起点比每次都从「我的预设.toml」删起好
    const QString base =
            m_preset.name.isEmpty() ? QString::fromLatin1(kUntitledPresetName) : m_preset.name;
    const QString start = m_presetPath.isEmpty()
                                  ? QDir(batchsmith::core::default_preset_directory())
                                            .filePath(base + QStringLiteral(".toml"))
                                  : m_presetPath;
    QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("另存为预设"), start, QStringLiteral("预设文件 (*.toml)"));
    if (path.isEmpty()) {
        return;  // 用户取消
    }
    if (!path.endsWith(QLatin1String(".toml"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".toml");
    }

    // 存成 `.local.toml` 会被当成绑定文件而读不到（list_preset_files 会跳过它）
    if (QFileInfo(path).completeBaseName().endsWith(QLatin1String(".local"))) {
        QMessageBox::warning(
                this,
                QStringLiteral("保存预设"),
                QStringLiteral("文件名不能以 .local 结尾 —— 那个后缀是留给本机绑定文件的，"
                               "用它当预设名会导致下次打不开。"));
        return;
    }

    // 失败时 savePresetTo 自己会弹框（窗口可见时）
    savePresetTo(path, nullptr);
}

bool MainWindow::savePresetTo(const QString& path, QString* error) {
    SlotBindings bindings;
    m_preset = preset_from_sources(
            m_listPanel->sources(), m_expressionBar->expression(), name_from_path(path), &bindings);
    m_preset.file_path = path;

    QString save_error;
    if (!batchsmith::core::save_preset(m_preset, path, &save_error)) {
        if (error != nullptr) {
            *error = save_error;
        }
        // 界面上的入口会弹框；直接调用的地方（测试/脚本）看返回值与 error
        if (isVisible()) {
            QMessageBox::warning(this, QStringLiteral("保存预设失败"), save_error);
        }
        return false;
    }

    // 绑定文件写失败**不算保存失败**：预设本身已经落盘，只是下次打开需要重绑一次。
    // 这件事要告诉用户，但不必拦着他。
    QString bind_error;
    const bool bindings_saved = batchsmith::core::save_bindings(bindings, path, &bind_error);

    m_presetPath = path;
    setDirty(false);
    updateWindowTitle();
    rememberRecent(path);  // 里面已经重建过「预设」菜单

    QString message = QStringLiteral("已保存：%1").arg(QDir::toNativeSeparators(path));
    if (!bindings_saved) {
        message += QStringLiteral("（路径绑定没存上：%1）").arg(bind_error);
    } else if (!bindings.isEmpty()) {
        message += QStringLiteral("（含 %1 个路径绑定）").arg(bindings.size());
    }
    // 存进预设目录的才提「预设」菜单 —— 另存到别处的文件不在那个菜单里，
    // 提了反而让人去那儿找
    if (QFileInfo(path).absolutePath() ==
        QFileInfo(batchsmith::core::default_preset_directory()).absoluteFilePath()) {
        message += QStringLiteral("　·　以后从「预设」菜单里可以一键切回来");
    }
    statusBar()->showMessage(message, 8000);

    if (!bindings_saved) {
        if (error != nullptr) {
            *error = bind_error;
        }
        if (isVisible()) {
            QMessageBox::warning(
                    this,
                    QStringLiteral("保存预设"),
                    QStringLiteral("预设已保存，但路径绑定文件没写成：\n%1").arg(bind_error));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 预设：管理
// ---------------------------------------------------------------------------

void MainWindow::showPresetManager() {
    if (m_presetManager == nullptr) {
        m_presetManager = new PresetManagerDialog(this);
        m_presetManager->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_presetManager, &QObject::destroyed, this, [this] { m_presetManager = nullptr; });

        connect(m_presetManager, &QDialog::accepted, this, [this] {
            // accepted 之后窗口才关、才 deleteLater，这里读它还是安全的
            const QString path = m_presetManager->selected_preset();
            if (path.isEmpty()) {
                return;
            }
            openPresetFromUi(path);
        });
    }

    m_presetManager->reload();
    m_presetManager->show();
    m_presetManager->raise();
    m_presetManager->activateWindow();
}

void MainWindow::revealPresetDirectory() {
    QString error;
    if (!batchsmith::core::ensure_preset_directory(&error)) {
        QMessageBox::warning(this, QStringLiteral("打开预设文件夹"), error);
        return;
    }
    // 用 QDesktopServices 而不是平台专用命令：三个平台都能用，也不启进程
    QDesktopServices::openUrl(QUrl::fromLocalFile(batchsmith::core::default_preset_directory()));
}

// ---------------------------------------------------------------------------
// 预设菜单（最近用过的那些，直接列出来）
// ---------------------------------------------------------------------------

QStringList MainWindow::recentPresets() const {
    QSettings settings;
    const QStringList stored = settings.value(QString::fromLatin1(kRecentKey)).toStringList();

    // 顺手清掉已经不在的 —— 列表里留一堆点不开的路径比空着更烦
    QStringList alive;
    for (const QString& path : stored) {
        if (QFileInfo::exists(path)) {
            alive.append(path);
        }
        if (alive.size() >= kMaxRecentPresets) {
            break;
        }
    }
    return alive;
}

void MainWindow::rememberRecent(const QString& path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();

    QStringList list = recentPresets();
    list.removeAll(absolute);
    list.prepend(absolute);
    while (list.size() > kMaxRecentPresets) {
        list.removeLast();
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kRecentKey), list);
    rebuildPresetMenu();
}

void MainWindow::rebuildPresetMenu() {
    if (m_presetMenu == nullptr) {
        return;
    }
    m_presetMenu->clear();

    const QString current =
            m_presetPath.isEmpty() ? QString() : QFileInfo(m_presetPath).absoluteFilePath();

    const QStringList list = recentPresets();
    if (list.isEmpty()) {
        // 空态也把"怎么才能有"写出来 —— 一个只有灰条的菜单比没有菜单更让人困惑
        auto* empty = m_presetMenu->addAction(QStringLiteral("（还没有预设）"));
        empty->setObjectName(QStringLiteral("presetMenuEmpty"));
        empty->setEnabled(false);
        auto* hint = m_presetMenu->addAction(QStringLiteral("配好之后按 Ctrl+S 就会存进预设目录"));
        hint->setEnabled(false);
    } else {
        for (const QString& path : list) {
            const QFileInfo info(path);
            auto* action = m_presetMenu->addAction(info.fileName());
            action->setObjectName(QStringLiteral("presetMenuRecent"));
            // 当前打开的那个打勾：菜单里一眼就能看出"我现在在哪一套里"
            action->setCheckable(true);
            action->setChecked(!current.isEmpty() && info.absoluteFilePath() == current);
            action->setData(path);
            action->setStatusTip(QDir::toNativeSeparators(path));
            action->setToolTip(QDir::toNativeSeparators(path));
            connect(action, &QAction::triggered, this, [this, path] { openPresetFromUi(path); });
        }
    }

    m_presetMenu->addSeparator();

    auto* manageAction = m_presetMenu->addAction(QStringLiteral("管理预设(&M)…"));
    manageAction->setObjectName(QStringLiteral("managePresetsAction"));
    manageAction->setStatusTip(
            QStringLiteral("列出预设目录里的全部预设，可打开 / 重命名 / 删除 / 创建快捷方式"));
    connect(manageAction, &QAction::triggered, this, &MainWindow::showPresetManager);

    auto* revealAction = m_presetMenu->addAction(QStringLiteral("打开预设文件夹"));
    revealAction->setObjectName(QStringLiteral("revealPresetsAction"));
    revealAction->setStatusTip(QStringLiteral("在文件管理器里打开预设目录"));
    connect(revealAction, &QAction::triggered, this, &MainWindow::revealPresetDirectory);
}

// ---------------------------------------------------------------------------
// 关闭
// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// 帮助
// ---------------------------------------------------------------------------

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
                           "<br>预设（列表 + 表达式）可从「文件」菜单保存与载入；"
                           "文件夹路径以 <code>${input}</code> 这样的槽位保存，"
                           "本机绑定另存在 <code>.local.toml</code> 里。"
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
