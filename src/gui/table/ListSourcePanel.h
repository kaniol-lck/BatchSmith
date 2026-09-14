#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

#include "batchsmith/core/list/list_source.hpp"

class QHBoxLayout;
class QScrollArea;
class ListSourceColumn;

/// 列表区：一个**水平滚动**的容器，里面并排 N 个列表列。
///
/// 结构就是需求里那两层：外层水平滚动（列多了横向滚），
/// 每列内部是竖向滚动的列表（见 ListSourceColumn）。
///
/// 编号规则（**这一条是设计决定，写在代码里以免以后被无声改掉**）：
///   * 已存在的列**永不改名**；
///   * 新增时取当前未被占用的最小编号。
///   于是删掉 list1 再新增，新的还是 list1；而 list2 永远叫 list2。
///   这样表达式（以及以后的预设）里的引用不会因为增删列而**静默指向另一个列表**——
///   那类错误在批量操作工具里代价很高。
class ListSourcePanel : public QWidget {
    Q_OBJECT

public:
    explicit ListSourcePanel(QWidget* parent = nullptr);

    /// 按屏幕上的列顺序导出（求值时按这个顺序取内层/外层循环）。
    [[nodiscard]] batchsmith::core::ListSourceList sources() const;

    [[nodiscard]] int columnCount() const { return static_cast<int>(m_columns.size()); }

public slots:
    /// 新增一列（名字由 nextName() 分配）
    void addColumn();

signals:
    /// 列数量或列内容发生变化
    void sourcesChanged();

private:
    void removeColumn(ListSourceColumn* column);

    /// 当前未被占用的最小编号
    [[nodiscard]] QString nextName() const;

    QScrollArea* m_scrollArea = nullptr;
    QHBoxLayout* m_columnLayout = nullptr;
    QList<ListSourceColumn*> m_columns;
};
