#include "result/ResultPanel.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QStringListModel>
#include <QVBoxLayout>

ResultPanel::ResultPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("resultPanel"));

    auto* title = new QLabel(QStringLiteral("输出列表"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("countLabel"));
    m_countLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    headerLayout->addWidget(m_countLabel);

    m_model = new QStringListModel(this);
    m_view = new QListView(this);
    m_view->setObjectName(QStringLiteral("resultView"));
    m_view->setModel(m_model);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setAlternatingRowColors(true);
    m_view->setUniformItemSizes(true);

    m_emptyHint = new QLabel(this);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(headerLayout);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_emptyHint);

    clear();
}

void ResultPanel::setRows(const QStringList& rows) {
    m_model->setStringList(rows);
    m_countLabel->setText(QStringLiteral("共 %1 行").arg(rows.size()));
    updateEmptyState();
}

void ResultPanel::clear() {
    m_model->setStringList({});
    m_countLabel->setText(QStringLiteral("共 0 行"));
    updateEmptyState();
}

void ResultPanel::updateEmptyState() {
    const bool empty = m_model->rowCount() == 0;
    m_emptyHint->setVisible(empty);
    if (empty) {
        m_emptyHint->setText(QStringLiteral("（还没有输出：在上方输入表达式后点「确定」）"));
    }
}
