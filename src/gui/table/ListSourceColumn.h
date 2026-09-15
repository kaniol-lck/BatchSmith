#pragma once

#include <QWidget>

#include "batchsmith/core/list/list_source.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMimeData;
class QStringListModel;
class QTimer;
class QToolButton;

/// 列表区里的一列：标题（列表名 + **来源模式**）+ 该模式的控件 + 列表内容。
///
/// ## 一列，多种来源模式
///
/// 需求是「列表本身可以作为多种模式切换」，所以这里不是两种列，而是同一列的两种取数方式：
///
///   * **手输**：直接键入条目（可增删改）。
///   * **文件夹**：绑定一个文件夹，按过滤与递归开关取数，列表只读 ——
///     想改就切回手输（切回去时**保留当前内容**作为起点，接着手改就行）。
///
/// 两种模式产出的是同一种东西（一列有序字符串），列名与编号都不随切换改变，
/// 于是表达式里的 `$list1[i]$` 不会因为换了来源而失效。
///
/// ## 为什么控件在上、列表在下
///
/// 路径与过滤是"规格"，列表是"结果"。从上到下读就是
/// 「我绑了哪儿 → 过滤什么 → 读到了什么」，反过来读会拧。
///
/// ## 宽度固定
///
/// 外层是水平滚动区，列若跟着拉伸就不会出现滚动条，「数量可以增减、多了就横向滚」
/// 这个交互就无从体现。高度交给布局撑满，列内的列表自己竖向滚动 —— 这正是需求里
/// 「外层水平滚动、每项内部竖向滚动」的两层结构。
class ListSourceColumn : public QWidget {
    Q_OBJECT

public:
    explicit ListSourceColumn(QString name, QWidget* parent = nullptr);

    [[nodiscard]] QString name() const { return m_name; }

    [[nodiscard]] batchsmith::core::ListSourceKind kind() const;

    /// 导出成 core 的列表源，供表达式求值使用。
    [[nodiscard]] batchsmith::core::ListSource source() const;

    /// 当前列表内容（界面上看到的那些条目）。
    [[nodiscard]] QStringList items() const;

    /// 切换来源模式。切到文件夹模式时按当前规格立刻取一次数。
    void setKind(batchsmith::core::ListSourceKind kind);

    /// 按一份来源规格设置这一列（加载预设时用）：模式、条目、文件夹规格都照它来。
    ///
    /// 文件夹模式会**重新取一次数**而不是照抄预设里的 `items` —— 预设存的是"从哪取"，
    /// 不是"取到了什么"；照抄反而会让用户看到上次打开时的旧内容。
    void applySpec(const batchsmith::core::ListSource& source);

    /// 绑定文件夹：切到文件夹模式、写入路径、立刻取数。
    ///
    /// 界面上的三条入口（拖放、点「…」选择、以及测试）都走这一个方法 ——
    /// 于是"拖进来"与"选进来"不会有行为差别。
    void bindDirectory(const QString& path);

    /// 按当前规格重新取数（文件夹模式才有意义）。取数失败时**保留上一次的结果**，
    /// 只在状态行上报错 —— 路径暂时不可达不该让整列突然空掉。
    void refreshDirectory();

    // 供离屏测试使用：界面上的开关就是它们
    [[nodiscard]] QComboBox* kindCombo() const { return m_kindCombo; }

    [[nodiscard]] QLineEdit* pathEdit() const { return m_pathEdit; }

    [[nodiscard]] QLineEdit* filterEdit() const { return m_filterEdit; }

    [[nodiscard]] QCheckBox* recursiveCheck() const { return m_recursiveCheck; }

    [[nodiscard]] QCheckBox* dirsCheck() const { return m_dirsCheck; }

    [[nodiscard]] QToolButton* refreshButton() const { return m_refreshButton; }

    [[nodiscard]] QLabel* statusLabel() const { return m_statusLabel; }

    [[nodiscard]] QListView* itemsView() const { return m_view; }

signals:
    /// 用户点了标题右侧的 ×
    void removeRequested(ListSourceColumn* column);

    /// 列表内容或来源规格发生变化
    void changed();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void applyKindToUi();
    void updateStatus(const QString& text, bool is_error);
    void chooseDirectory();

    void appendItem();
    void removeSelectedItems();

    /// 从拖放数据里取出第一个本地文件夹（没有则返回空）
    [[nodiscard]] static QString directoryFromMimeData(const QMimeData* mime);

    QString m_name;

    QComboBox* m_kindCombo = nullptr;
    QLabel* m_titleLabel = nullptr;

    QWidget* m_dirControls = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QToolButton* m_browseButton = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QCheckBox* m_recursiveCheck = nullptr;
    QCheckBox* m_dirsCheck = nullptr;
    QToolButton* m_refreshButton = nullptr;
    QLabel* m_statusLabel = nullptr;

    QWidget* m_manualFooter = nullptr;
    QListView* m_view = nullptr;
    QStringListModel* m_model = nullptr;

    /// 防抖：连续改动（敲路径、切换开关）只在停顿后扫一次
    QTimer* m_refreshTimer = nullptr;
};
