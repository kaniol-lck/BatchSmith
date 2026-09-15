#pragma once

#include <functional>

#include <QDialog>
#include <QString>
#include <QStringList>

#include "batchsmith/core/preset/preset.hpp"

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// 预设管理：列出预设目录里的文件，可以打开、重命名、删除、**创建快捷方式**。
///
/// 为什么要有这个对话框（而不是让用户去文件管理器里翻）：
///   * 预设目录在各平台的路径不一样，用户未必知道在哪；
///   * **读不动的预设也要能看见**（手改坏了、版本过高），否则用户只会在
///     "打开预设"里撞一鼻子灰，却找不到是哪个文件的问题；
///   * 删除时要连**伴生绑定文件**一起处理 —— 那是本机信息，用户很难想到。
///
/// 重命名会同时改**文件名**与文件里的 `[preset].name`：两个名字不一致时，
/// 界面标题、最近打开、文件管理器里显示的名字会各不相同，那种混乱很难解释。
class PresetManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PresetManagerDialog(QWidget* parent = nullptr);

    /// 用户选择要打开的那个预设文件的完整路径；取消时为空。
    [[nodiscard]] QString selected_preset() const { return m_selected; }

    /// 重新扫描预设目录并刷新列表
    void reload();

    /// 快捷方式建到哪个目录。默认（空）表示**弹对话框问用户**；
    /// 设了值就直接用它、不弹框（离屏测试用这个，免得往开发机真实桌面放东西）。
    void setShortcutDirectory(const QString& directory) { m_shortcutDirectory = directory; }

    // ---- 「做事」与「弹框」分开 ----
    //
    // 这两个方法就是按钮真正做的事（`editNote()` / `chooseIcon()` 只负责弹框，然后调它们）。
    // 分开的理由不只是好测：**模态框会把离屏测试卡死**（没有超时、没有输出，
    // 看起来像死循环），所以凡是要弹框的路径都得有一条"不弹框也能做"的入口。

    /// 改预设文件里的备注（写进 `[preset] note`，跟着预设文件走）
    bool setNote(const QString& preset_path, const QString& note, QString* error = nullptr);

    /// 改本机设置里的快捷方式图标（写进 `.local.toml`，**不进预设文件**）。
    /// `icon_path` 为空 = 清除。
    bool setShortcutIcon(const QString& preset_path,
                         const QString& icon_path,
                         QString* error = nullptr);

    // 供离屏测试使用
    [[nodiscard]] QTreeWidget* tree() const { return m_tree; }

    [[nodiscard]] QPushButton* openButton() const { return m_openButton; }

    [[nodiscard]] QPushButton* renameButton() const { return m_renameButton; }

    [[nodiscard]] QPushButton* noteButton() const { return m_noteButton; }

    [[nodiscard]] QPushButton* iconButton() const { return m_iconButton; }

    [[nodiscard]] QPushButton* clearIconButton() const { return m_clearIconButton; }

    [[nodiscard]] QPushButton* shortcutButton() const { return m_shortcutButton; }

    [[nodiscard]] QPushButton* removeButton() const { return m_removeButton; }

    [[nodiscard]] QLabel* statusLabel() const { return m_statusLabel; }

    /// 各列的下标（测试与文档都按名字来，别写魔法数字）
    enum Column {
        ColumnPreset = 0,
        ColumnLists,
        ColumnTemplate,
        ColumnNote,
        ColumnModified,
        ColumnFile
    };

private:
    void updateButtons();
    [[nodiscard]] QStringList selectedPaths() const;

    /// 选中恰好一个预设时的那一行；没有/多选返回空
    [[nodiscard]] QString singleSelectedPath() const;

    void openSelected();

    // 这三个是"按钮 + 弹框"的那一层，真正的动作在上面两个 set* 里
    void renameSelected();
    void editNote();
    void chooseIcon();
    void clearIcon();
    void removeSelected();
    void createShortcut();
    void revealDirectory();

    void addRow(const QString& path);

    /// 改这个预设文件里的某个字段后写回（读 → 改 → 存）。
    /// 用于备注这类**存在预设文件里**的字段。失败时自己弹框，返回是否成功。
    bool updatePresetFile(const QString& path,
                          const std::function<void(batchsmith::core::Preset&)>& change,
                          QString* error);

    QTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_noteButton = nullptr;
    QPushButton* m_iconButton = nullptr;
    QPushButton* m_clearIconButton = nullptr;
    QPushButton* m_shortcutButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QString m_shortcutDirectory;
    QString m_selected;
};
