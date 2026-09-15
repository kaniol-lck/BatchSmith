#include "preset/PresetManagerDialog.h"

#include <utility>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "batchsmith/core/preset/preset.hpp"
#include "preset/PresetShortcut.h"

namespace {

using batchsmith::core::bindings_path_for;
using batchsmith::core::load_local_settings;
using batchsmith::core::load_preset;

}  // namespace

PresetManagerDialog::PresetManagerDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("presetManagerDialog"));
    setWindowTitle(QStringLiteral("管理预设"));
    resize(900, 480);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("presetTree"));
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("预设"),
                             QStringLiteral("列表"),
                             QStringLiteral("输出表达式"),
                             QStringLiteral("备注"),
                             QStringLiteral("修改时间"),
                             QStringLiteral("文件")});
    m_tree->header()->setStretchLastSection(false);
    // 「输出表达式」是最宽、也最需要看清的一列；「文件」只是"到底是哪个文件"的兜底
    m_tree->header()->setSectionResizeMode(ColumnPreset, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColumnLists, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColumnTemplate, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColumnNote, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColumnModified, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColumnFile, QHeaderView::ResizeToContents);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &PresetManagerDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) {
        openSelected();  // 双击即打开
    });

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("presetStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    m_openButton = new QPushButton(QStringLiteral("打开"), this);
    m_openButton->setObjectName(QStringLiteral("presetOpenButton"));
    m_openButton->setDefault(true);
    connect(m_openButton, &QPushButton::clicked, this, &PresetManagerDialog::openSelected);

    m_renameButton = new QPushButton(QStringLiteral("重命名…"), this);
    m_renameButton->setObjectName(QStringLiteral("presetRenameButton"));
    connect(m_renameButton, &QPushButton::clicked, this, &PresetManagerDialog::renameSelected);

    m_noteButton = new QPushButton(QStringLiteral("备注…"), this);
    m_noteButton->setObjectName(QStringLiteral("presetNoteButton"));
    m_noteButton->setStatusTip(QStringLiteral("写一句「这套是干什么的」，跟着预设文件走"));
    connect(m_noteButton, &QPushButton::clicked, this, &PresetManagerDialog::editNote);

    m_iconButton = new QPushButton(QStringLiteral("图标…"), this);
    m_iconButton->setObjectName(QStringLiteral("presetIconButton"));
    m_iconButton->setStatusTip(QStringLiteral("给这个预设挑一个图标（本机设置，不进预设文件；"
                                              "建快捷方式时会用它）"));
    connect(m_iconButton, &QPushButton::clicked, this, &PresetManagerDialog::chooseIcon);

    m_clearIconButton = new QPushButton(QStringLiteral("清除图标"), this);
    m_clearIconButton->setObjectName(QStringLiteral("presetClearIconButton"));
    connect(m_clearIconButton, &QPushButton::clicked, this, &PresetManagerDialog::clearIcon);

    m_shortcutButton = new QPushButton(QStringLiteral("创建快捷方式…"), this);
    m_shortcutButton->setObjectName(QStringLiteral("presetShortcutButton"));
    m_shortcutButton->setStatusTip(
            QStringLiteral("挑一个目录，在里面建一个「双击就用这个预设打开」的快捷方式"));
    connect(m_shortcutButton, &QPushButton::clicked, this, &PresetManagerDialog::createShortcut);

    m_removeButton = new QPushButton(QStringLiteral("删除"), this);
    m_removeButton->setObjectName(QStringLiteral("presetRemoveButton"));
    connect(m_removeButton, &QPushButton::clicked, this, &PresetManagerDialog::removeSelected);

    auto* revealButton = new QPushButton(QStringLiteral("打开预设文件夹"), this);
    revealButton->setObjectName(QStringLiteral("presetRevealButton"));
    connect(revealButton, &QPushButton::clicked, this, &PresetManagerDialog::revealDirectory);

    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setObjectName(QStringLiteral("presetCloseButton"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    // 两行：上一行都是"改这个预设自己"，下一行是"拿它去做点别的 / 离开"
    auto* editLayout = new QHBoxLayout;
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->addWidget(m_openButton);
    editLayout->addWidget(m_renameButton);
    editLayout->addWidget(m_noteButton);
    editLayout->addWidget(m_iconButton);
    editLayout->addWidget(m_clearIconButton);
    editLayout->addWidget(m_removeButton);
    editLayout->addStretch();

    auto* actionLayout = new QHBoxLayout;
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->addWidget(m_shortcutButton);
    actionLayout->addStretch();
    actionLayout->addWidget(revealButton);
    actionLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_statusLabel);
    layout->addLayout(editLayout);
    layout->addLayout(actionLayout);

    reload();
}

void PresetManagerDialog::reload() {
    m_tree->clear();

    const QStringList files = batchsmith::core::list_preset_files();
    for (const QString& path : files) {
        addRow(path);
    }

    const QString directory =
            QDir::toNativeSeparators(batchsmith::core::default_preset_directory());
    if (files.isEmpty()) {
        m_statusLabel->setText(
                QStringLiteral("这个目录里还没有预设。\n在界面上配好之后按 Ctrl+S（或「文件 → "
                               "保存预设」）就会存到这里。\n目录：%1")
                        .arg(directory));
    } else {
        m_statusLabel->setText(
                QStringLiteral("共 %1 个预设。目录：%2").arg(files.size()).arg(directory));
    }
    updateButtons();
}

void PresetManagerDialog::addRow(const QString& path) {
    const QFileInfo info(path);

    auto* item = new QTreeWidgetItem(m_tree);
    item->setData(0, Qt::UserRole, path);

    const auto loaded = load_preset(path);
    if (loaded.ok()) {
        item->setText(ColumnPreset, loaded.preset.name);
        item->setText(ColumnLists, QString::number(loaded.preset.lists.size()));
        // 输出表达式是"这个预设到底会做什么"最直接的答案，直接摊在一列里；
        // 空模板给「（空）」而不是留白，免得看起来像读失败了
        item->setText(ColumnTemplate,
                      loaded.preset.template_text.isEmpty() ? QStringLiteral("（空）")
                                                            : loaded.preset.template_text);
        item->setToolTip(ColumnTemplate, loaded.preset.template_text);
        item->setText(ColumnNote,
                      loaded.preset.note.isEmpty() ? QStringLiteral("—") : loaded.preset.note);
        item->setToolTip(ColumnNote, loaded.preset.note);
    } else {
        // 读不动的预设也要显示出来 —— 否则用户只会在"打开预设"里撞墙，
        // 却不知道是哪个文件坏了，也删不掉它
        const QString unreadable = QStringLiteral("（读不了）");
        for (int column = ColumnLists; column <= ColumnNote; ++column) {
            item->setText(column, QStringLiteral("—"));
        }
        item->setText(ColumnPreset, unreadable);
        item->setForeground(ColumnPreset, QBrush(QColor(0xc0, 0x39, 0x2b)));
        item->setToolTip(ColumnPreset, loaded.error);
        item->setToolTip(ColumnLists, loaded.error);
    }

    // 自定义图标（本机设置）直接画在名字旁边 —— 与文件管理器里看图标是一个意思
    const QString icon_path = load_local_settings(path).shortcut_icon;
    if (!icon_path.isEmpty() && QFileInfo::exists(icon_path)) {
        item->setIcon(ColumnPreset, QIcon(icon_path));
        item->setToolTip(
                ColumnPreset,
                QStringLiteral("%1\n图标：%2")
                        .arg(item->text(ColumnPreset), QDir::toNativeSeparators(icon_path)));
    } else if (!icon_path.isEmpty()) {
        // 图标文件被删了/挪走了：不静默当作没设过 —— 用户会以为"我明明设过"
        item->setToolTip(
                ColumnPreset,
                QStringLiteral("%1\n图标已经不在了：%2")
                        .arg(item->text(ColumnPreset), QDir::toNativeSeparators(icon_path)));
    }

    item->setText(ColumnModified, info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    item->setText(ColumnFile, info.fileName());
    item->setToolTip(ColumnFile, QDir::toNativeSeparators(path));
}

QString PresetManagerDialog::singleSelectedPath() const {
    const QStringList paths = selectedPaths();
    return paths.size() == 1 ? paths.first() : QString();
}

QStringList PresetManagerDialog::selectedPaths() const {
    QStringList paths;
    const QList<QTreeWidgetItem*> items = m_tree->selectedItems();
    paths.reserve(items.size());
    for (const QTreeWidgetItem* item : items) {
        paths.append(item->data(0, Qt::UserRole).toString());
    }
    return paths;
}

void PresetManagerDialog::updateButtons() {
    const QStringList paths = selectedPaths();
    const bool one = paths.size() == 1;
    m_openButton->setEnabled(one);
    m_renameButton->setEnabled(one);
    m_noteButton->setEnabled(one);
    m_iconButton->setEnabled(one);
    m_shortcutButton->setEnabled(one);
    m_removeButton->setEnabled(!paths.isEmpty());

    // 「清除图标」只在**确实设过**图标时可用 —— 与其它按钮一样，
    // 不给一个点了没反应的按钮
    bool has_icon = false;
    if (one) {
        has_icon = !load_local_settings(paths.first()).shortcut_icon.isEmpty();
    }
    m_clearIconButton->setEnabled(has_icon);
}

void PresetManagerDialog::openSelected() {
    const QStringList paths = selectedPaths();
    if (paths.size() != 1) {
        return;
    }
    m_selected = paths.first();
    accept();
}

void PresetManagerDialog::renameSelected() {
    const QStringList paths = selectedPaths();
    if (paths.size() != 1) {
        return;
    }
    const QString old_path = paths.first();
    const QFileInfo info(old_path);

    bool accepted = false;
    const QString new_name =
            QInputDialog::getText(this,
                                  QStringLiteral("重命名预设"),
                                  QStringLiteral("新名字（文件名与预设名一起改）："),
                                  QLineEdit::Normal,
                                  info.completeBaseName(),
                                  &accepted)
                    .trimmed();
    if (!accepted || new_name.isEmpty() || new_name == info.completeBaseName()) {
        return;
    }
    if (new_name.contains(QLatin1Char('/')) || new_name.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(
                this, QStringLiteral("重命名预设"), QStringLiteral("名字里不能有路径分隔符。"));
        return;
    }

    const QString new_path = info.absoluteDir().filePath(new_name + QStringLiteral(".toml"));
    if (QFileInfo::exists(new_path)) {
        QMessageBox::warning(
                this,
                QStringLiteral("重命名预设"),
                QStringLiteral("已经有同名文件了：%1").arg(QDir::toNativeSeparators(new_path)));
        return;
    }

    // 先按新名字写新文件、再删旧的：中途失败也不会把预设弄丢
    const auto loaded = load_preset(old_path);
    if (!loaded.ok()) {
        QMessageBox::warning(this,
                             QStringLiteral("重命名预设"),
                             QStringLiteral("这个文件读不了，不能重命名：\n%1").arg(loaded.error));
        return;
    }

    batchsmith::core::Preset preset = loaded.preset;
    preset.name = new_name;
    preset.file_path.clear();

    QString error;
    if (!batchsmith::core::save_preset(preset, new_path, &error)) {
        QMessageBox::warning(this, QStringLiteral("重命名预设"), error);
        return;
    }
    if (!QFile::remove(old_path)) {
        QMessageBox::warning(this,
                             QStringLiteral("重命名预设"),
                             QStringLiteral("新文件已写出，但旧文件删不掉：%1")
                                     .arg(QDir::toNativeSeparators(old_path)));
    }

    // 伴生绑定文件跟着改名 —— 它是本机信息，用户很难想到要自己处理
    const QString old_bindings = bindings_path_for(old_path);
    if (QFileInfo::exists(old_bindings)) {
        QFile::rename(old_bindings, bindings_path_for(new_path));
    }

    reload();
}

void PresetManagerDialog::removeSelected() {
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        return;
    }

    QStringList names;
    for (const QString& path : paths) {
        names.append(QFileInfo(path).fileName());
    }
    const auto answer =
            QMessageBox::question(this,
                                  QStringLiteral("删除预设"),
                                  QStringLiteral("要删除这 %1 个预设吗？\n\n%2\n\n"
                                                 "（同名的 .local.toml 绑定文件也会一并删掉）")
                                          .arg(paths.size())
                                          .arg(names.join(QStringLiteral("\n"))),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QStringList failed;
    for (const QString& path : paths) {
        if (!QFile::remove(path)) {
            failed.append(QFileInfo(path).fileName());
            continue;
        }
        QFile::remove(bindings_path_for(path));  // 不存在就算了
    }

    reload();
    if (!failed.isEmpty()) {
        QMessageBox::warning(
                this,
                QStringLiteral("删除预设"),
                QStringLiteral("这几个删不掉：\n%1").arg(failed.join(QStringLiteral("\n"))));
    }
}

void PresetManagerDialog::createShortcut() {
    const QString preset_path = singleSelectedPath();
    if (preset_path.isEmpty()) {
        return;
    }

    // 建到哪由用户挑。默认落在桌面 —— 那是"双击"这个动作最自然的地方，
    // 但**不替他决定**：快捷方式经常是要放到某个项目文件夹、启动器目录里的。
    // 测试会先把 m_shortcutDirectory 设好，于是这里不弹框。
    QString directory = m_shortcutDirectory;
    if (directory.isEmpty()) {
        directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("快捷方式放到哪"), default_shortcut_directory());
        if (directory.isEmpty()) {
            return;  // 用户取消
        }
    }

    // 图标是可选的：设过就用它，没设过就用程序自带图标
    const ShortcutRequest request(preset_path,
                                  directory,
                                  batchsmith::core::load_local_settings(preset_path).shortcut_icon);

    QString error;
    const QString created = create_preset_shortcut(request, &error);
    if (created.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("创建快捷方式"), error);
        return;
    }

    // 成功**不弹框**：给几个预设连着建快捷方式是正常用法，每建一个就要点一次
    // "确定"很烦。结果写在状态行里（就在按钮上方，看得见），
    // 只有失败才用弹框打断。
    m_statusLabel->setText(
            QStringLiteral("已创建快捷方式：%1\n双击它就会用「%2」这个预设打开 BatchSmith。"
                           "以后改预设内容不用重建快捷方式 —— 它指向的是预设文件本身。")
                    .arg(QDir::toNativeSeparators(created),
                         QFileInfo(preset_path).completeBaseName()));
    m_statusLabel->setToolTip(QDir::toNativeSeparators(QFileInfo(created).absolutePath()));
}

bool PresetManagerDialog::updatePresetFile(
        const QString& path,
        const std::function<void(batchsmith::core::Preset&)>& change,
        QString* error) {
    const auto loaded = load_preset(path);
    if (!loaded.ok()) {
        if (error != nullptr) {
            *error = loaded.error;
        }
        QMessageBox::warning(this,
                             QStringLiteral("管理预设"),
                             QStringLiteral("这个文件读不了，不能改：\n%1").arg(loaded.error));
        return false;
    }

    batchsmith::core::Preset preset = loaded.preset;
    preset.file_path.clear();
    change(preset);

    QString save_error;
    if (!batchsmith::core::save_preset(preset, path, &save_error)) {
        if (error != nullptr) {
            *error = save_error;
        }
        QMessageBox::warning(this, QStringLiteral("管理预设"), save_error);
        return false;
    }
    reload();
    return true;
}

bool PresetManagerDialog::setNote(const QString& preset_path, const QString& note, QString* error) {
    return updatePresetFile(
            preset_path, [&note](batchsmith::core::Preset& preset) { preset.note = note; }, error);
}

bool PresetManagerDialog::setShortcutIcon(const QString& preset_path,
                                          const QString& icon_path,
                                          QString* error) {
    // 先读回来再整体写回：`.local.toml` 里还有槽位绑定，只写图标那一节会把它抹掉
    batchsmith::core::LocalSettings settings = batchsmith::core::load_local_settings(preset_path);
    settings.shortcut_icon = icon_path;

    QString save_error;
    if (!batchsmith::core::save_local_settings(settings, preset_path, &save_error)) {
        if (error != nullptr) {
            *error = save_error;
        }
        QMessageBox::warning(this, QStringLiteral("管理预设"), save_error);
        return false;
    }
    reload();
    return true;
}

void PresetManagerDialog::editNote() {
    const QString path = singleSelectedPath();
    if (path.isEmpty()) {
        return;
    }

    const auto loaded = load_preset(path);
    if (!loaded.ok()) {
        return;  // updatePresetFile 会给出可读的报错
    }

    bool accepted = false;
    const QString note = QInputDialog::getMultiLineText(this,
                                                        QStringLiteral("备注"),
                                                        QStringLiteral("给「%1」写一句备注："
                                                                       "（跟着预设文件走，"
                                                                       "分享时会一起带过去）")
                                                                .arg(loaded.preset.name),
                                                        loaded.preset.note,
                                                        &accepted);
    if (!accepted) {
        return;
    }
    if (setNote(path, note, nullptr)) {
        m_statusLabel->setText(note.isEmpty() ? QStringLiteral("已清除备注")
                                              : QStringLiteral("已记下备注：%1").arg(note));
    }
}

void PresetManagerDialog::chooseIcon() {
    const QString path = singleSelectedPath();
    if (path.isEmpty()) {
        return;
    }

    // 图标是**本机路径**，存进 `.local.toml`（与槽位绑定同一份文件）——
    // 这样预设本身还能干净地发给别人
    const QString current = batchsmith::core::load_local_settings(path).shortcut_icon;
#if defined(Q_OS_WIN)
    // Windows 上还能直接指一个 exe/dll —— 从里面挑图标是常见做法
    const QString filter = QStringLiteral("图标或程序 (*.ico *.png *.bmp *.exe *.dll);;"
                                          "所有文件 (*)");
#else
    const QString filter = QStringLiteral("图标 (*.png *.svg *.ico *.xpm);;所有文件 (*)");
#endif
    const QString icon =
            QFileDialog::getOpenFileName(this,
                                         QStringLiteral("挑一个图标"),
                                         current.isEmpty() ? QDir::homePath() : current,
                                         filter);
    if (icon.isEmpty()) {
        return;  // 用户取消
    }

    const QString absolute = QFileInfo(icon).absoluteFilePath();
    if (setShortcutIcon(path, absolute, nullptr)) {
        m_statusLabel->setText(QStringLiteral("已设置图标：%1（本机设置，不进预设文件）")
                                       .arg(QDir::toNativeSeparators(absolute)));
    }
}

void PresetManagerDialog::clearIcon() {
    const QString path = singleSelectedPath();
    if (path.isEmpty()) {
        return;
    }
    if (setShortcutIcon(path, QString(), nullptr)) {
        m_statusLabel->setText(QStringLiteral("已清除图标 —— 以后建的快捷方式用程序自带图标"));
    }
}

void PresetManagerDialog::revealDirectory() {
    QString error;
    if (!batchsmith::core::ensure_preset_directory(&error)) {
        QMessageBox::warning(this, QStringLiteral("打开预设文件夹"), error);
        return;
    }
    const QString directory = batchsmith::core::default_preset_directory();
    // 用 QDesktopServices 而不是平台专用命令：三个平台都能用，也不启进程
    QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
}
