#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "batchsmith/core/preset/preset.hpp"

class CheatsheetDialog;
class ExpressionBar;
class ListSourcePanel;
class PresetManagerDialog;
class ResultPanel;
class QAction;
class QMenu;
class QSplitter;

/// BatchSmith 主窗口：**上下两段**。
///
/// ```text
/// ┌──────────────────────────────────────────────────────┐
/// │ 列表区（水平滚动）                                    │
/// │ ┌────────┐ ┌────────┐ ┌────────┐                     │
/// │ │ list1  │ │ list2  │ │ list3  │  ← 每列内部竖向滚动  │
/// │ │  1     │ │  a     │ │  x     │     ＋/× 控制列数量   │
/// │ │  2     │ │  b     │ │  y     │                     │
/// │ └────────┘ └────────┘ └────────┘                     │
/// ├──────────────────────────────────────────────────────┤
/// │ [ 表达式__________________________ ] [ 确定 ]         │
/// │ 提示 / 报错                                          │
/// │ 输出列表                                             │
/// │   mv 1 out                                           │
/// │   mv 2 out                                           │
/// └──────────────────────────────────────────────────────┘
/// ```
///
/// 界面本身**不含任何求值逻辑**：点的「确定」只是把当前列与表达式交给
/// `batchsmith::core::dsl::evaluate_template`，再把结果铺到输出列表上。
///
/// 这条分层约定在 Phase 2 被验证过一次：core 的求值实现从"简单求值器"换成
/// "Lua 编译器 + 沙箱"时，这个文件确实一行都不用改。后续阶段继续照此办理。
///
/// ## 预设
///
/// 「文件」菜单可以新建 / 打开 / 保存 / 另存为 / 管理预设；启动时也可以直接带上
/// 一个预设文件（`batchsmith xxx.toml`）。窗口标题显示当前预设名与未保存标记，
/// 关闭前若有未保存的改动会先问一句。
///
/// 预设里的文件夹路径是**运行时槽位**（`${input}`），本机绑定的实际路径存在预设
/// 旁边的 `.local.toml` 里 —— 于是预设本身可以进版本控制、可以发给别人。
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// 打开一个预设文件。命令行启动加载走的也是它。
    ///
    /// 单列读不到（比如某个槽位还没绑定路径）**不算失败**：预设照常打开，
    /// 那一列是空的并在界面上给出提示 —— 用户往往只是想改个路径。
    /// 只有"文件本身读不了/格式不对"才算失败。
    ///
    /// @param error 失败时写入原因
    /// @return 是否成功打开
    bool openPreset(const QString& path, QString* error = nullptr);

    /// 把界面上的东西收进 `m_preset` 并**保存到指定路径**（含伴生绑定文件）。
    ///
    /// 公开是因为"保存到某个路径"本身就是个完整动作：菜单的「保存 / 另存为」只是
    /// 先问出路径，测试与将来的脚本化入口可以直接给它路径。
    ///
    /// 文件夹路径在这一步被**槽位化**：预设文件里只留 `${input}`，实际路径写进
    /// `<预设名>.local.toml`。绑定文件写失败**不算保存失败**（预设已经落盘了），
    /// 但会把原因写进 `error`。
    bool savePresetTo(const QString& path, QString* error = nullptr);

    // 以下供离屏测试核对状态（界面上的入口就是它们）
    [[nodiscard]] QString presetPath() const { return m_presetPath; }

    [[nodiscard]] QString presetName() const { return m_preset.name; }

    [[nodiscard]] bool hasUnsavedChanges() const { return m_dirty; }

    [[nodiscard]] ListSourcePanel* listPanel() const { return m_listPanel; }

    [[nodiscard]] ExpressionBar* expressionBar() const { return m_expressionBar; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void evaluateExpression(const QString& expression);
    void refreshListSummary();

    void newPreset();
    void choosePresetToOpen();
    void savePreset();
    void savePresetAs();
    void showPresetManager();
    void revealPresetDirectory();

    void showAbout();
    void showCheatsheet();

private:
    void buildCentralLayout();
    void buildMenus();

    void setDirty(bool dirty);
    void updateWindowTitle();

    /// 有未保存改动时问一句。返回 true 表示可以继续（用户选了保存或放弃）。
    [[nodiscard]] bool confirmDiscardChanges();

    void rememberRecent(const QString& path);
    void rebuildRecentMenu();
    [[nodiscard]] QStringList recentPresets() const;

    QSplitter* m_topBottomSplitter = nullptr;
    ListSourcePanel* m_listPanel = nullptr;
    ExpressionBar* m_expressionBar = nullptr;
    ResultPanel* m_resultPanel = nullptr;

    /// 帮助窗口只保持一个实例（非模态，见 showCheatsheet）
    CheatsheetDialog* m_cheatsheet = nullptr;
    /// 预设管理对话框同理
    PresetManagerDialog* m_presetManager = nullptr;

    batchsmith::core::Preset m_preset;
    QString m_presetPath;  ///< 空表示还没保存过（标题里显示"未命名"）
    bool m_dirty = false;

    QMenu* m_recentMenu = nullptr;
    QAction* m_saveAction = nullptr;
};
