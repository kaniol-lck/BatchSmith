#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// 预设管理：列出预设目录里的文件，可以打开、重命名、删除。
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

    // 供离屏测试使用
    [[nodiscard]] QTreeWidget* tree() const { return m_tree; }

    [[nodiscard]] QPushButton* openButton() const { return m_openButton; }

    [[nodiscard]] QPushButton* renameButton() const { return m_renameButton; }

    [[nodiscard]] QPushButton* removeButton() const { return m_removeButton; }

    [[nodiscard]] QLabel* statusLabel() const { return m_statusLabel; }

private:
    void updateButtons();
    [[nodiscard]] QStringList selectedPaths() const;

    void openSelected();

    // 三个动作刻意做成 public 之外也能被测试直接触发（点按钮就是调它们）
    void renameSelected();
    void removeSelected();
    void revealDirectory();

    void addRow(const QString& path);

    QTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QString m_selected;
};
