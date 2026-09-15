#include "preset/PresetManagerDialog.h"

#include <utility>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
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
using batchsmith::core::load_preset;

}  // namespace

PresetManagerDialog::PresetManagerDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("presetManagerDialog"));
    setWindowTitle(QStringLiteral("管理预设"));
    resize(680, 440);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("presetTree"));
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("预设"),
                             QStringLiteral("列表"),
                             QStringLiteral("修改时间"),
                             QStringLiteral("文件")});
    m_tree->header()->setStretchLastSection(true);
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

    m_removeButton = new QPushButton(QStringLiteral("删除"), this);
    m_removeButton->setObjectName(QStringLiteral("presetRemoveButton"));
    connect(m_removeButton, &QPushButton::clicked, this, &PresetManagerDialog::removeSelected);

    m_shortcutButton = new QPushButton(QStringLiteral("创建快捷方式"), this);
    m_shortcutButton->setObjectName(QStringLiteral("presetShortcutButton"));
    m_shortcutButton->setStatusTip(
            QStringLiteral("在桌面建一个快捷方式，双击它就用这个预设打开 BatchSmith"));
    m_shortcutButton->setToolTip(
            QStringLiteral("在桌面建一个快捷方式，双击它就用这个预设打开 BatchSmith"));
    connect(m_shortcutButton, &QPushButton::clicked, this, &PresetManagerDialog::createShortcut);

    auto* revealButton = new QPushButton(QStringLiteral("打开预设文件夹"), this);
    revealButton->setObjectName(QStringLiteral("presetRevealButton"));
    connect(revealButton, &QPushButton::clicked, this, &PresetManagerDialog::revealDirectory);

    auto* closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setObjectName(QStringLiteral("presetCloseButton"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(m_openButton);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(m_shortcutButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(revealButton);
    buttonLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_statusLabel);
    layout->addLayout(buttonLayout);

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
        item->setText(0, loaded.preset.name);
        item->setText(1, QString::number(loaded.preset.lists.size()));
    } else {
        // 读不动的预设也要显示出来 —— 否则用户只会在"打开预设"里撞墙，
        // 却不知道是哪个文件坏了，也删不掉它
        item->setText(0, QStringLiteral("（读不了）"));
        item->setText(1, QStringLiteral("—"));
        item->setForeground(0, QBrush(QColor(0xc0, 0x39, 0x2b)));
        item->setToolTip(0, loaded.error);
    }
    item->setText(2, info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    item->setText(3, info.fileName());
    item->setToolTip(3, QDir::toNativeSeparators(path));
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
    m_shortcutButton->setEnabled(one);
    m_removeButton->setEnabled(!paths.isEmpty());
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
    const QStringList paths = selectedPaths();
    if (paths.size() != 1) {
        return;
    }

    QString error;
    const QString created = create_preset_shortcut(paths.first(), m_shortcutDirectory, &error);
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
                         QFileInfo(paths.first()).completeBaseName()));
    m_statusLabel->setToolTip(QDir::toNativeSeparators(QFileInfo(created).absolutePath()));
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
