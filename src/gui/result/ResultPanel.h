#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QListView;
class QStringListModel;

/// 输出区：把表达式算出来的行按顺序列出来。
///
/// 只读列表（输出不是输入）。空结果时给一句说明 —— 空白的列表看不出
/// 「是没算，还是算出来是空」，而这两件事在批量操作里差别很大。
class ResultPanel : public QWidget {
    Q_OBJECT

public:
    explicit ResultPanel(QWidget* parent = nullptr);

    void setRows(const QStringList& rows);

    /// 还没算过时的状态（清空并提示）
    void clear();

private:
    void updateEmptyState();

    QListView* m_view = nullptr;
    QStringListModel* m_model = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_emptyHint = nullptr;
};
