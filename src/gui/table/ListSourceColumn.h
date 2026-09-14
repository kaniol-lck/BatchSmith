#pragma once

#include <QWidget>

#include "batchsmith/core/list/list_source.hpp"

class QLabel;
class QListView;
class QStringListModel;

/// 列表区里的一列：标题（列表名）+ 一个竖向滚动的列表 + 增删项按钮。
///
/// 宽度**固定**：外层是水平滚动区，列若跟着拉伸就不会出现滚动条，
/// 「数量可以增减、多了就横向滚」这个交互就无从体现。
/// 高度交给布局撑满，列内的列表自己竖向滚动 —— 这正是需求里
/// 「外层水平滚动、每项内部竖向滚动」的两层结构。
class ListSourceColumn : public QWidget {
    Q_OBJECT

public:
    explicit ListSourceColumn(QString name, QWidget* parent = nullptr);

    [[nodiscard]] QString name() const { return m_name; }

    /// 导出成 core 的列表源，供表达式求值使用。
    [[nodiscard]] batchsmith::core::ListSource source() const;

signals:
    /// 用户点了标题右侧的 ×
    void removeRequested(ListSourceColumn* column);

    /// 列表内容发生变化（增删项、编辑项）
    void changed();

private:
    void appendItem();
    void removeSelectedItems();

    QString m_name;
    QLabel* m_titleLabel = nullptr;
    QListView* m_view = nullptr;
    QStringListModel* m_model = nullptr;
};
