#include "table/ListSourceColumn.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QAbstractItemView>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QStringListModel>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kColumnWidth = 190;
}

ListSourceColumn::ListSourceColumn(QString name, QWidget* parent)
    : QWidget(parent), m_name(std::move(name)) {
    setFixedWidth(kColumnWidth);
    // 用列表名做对象名：既便于离屏 GUI 测试按名字找到某一列（findChild("list2")），
    // 也让 Qt 的对象树在调试时一眼能读。
    setObjectName(m_name);

    m_titleLabel = new QLabel(m_name, this);
    m_titleLabel->setObjectName(QStringLiteral("titleLabel"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    auto* removeButton = new QToolButton(this);
    removeButton->setObjectName(QStringLiteral("removeColumnButton"));
    removeButton->setText(QStringLiteral("×"));
    removeButton->setAutoRaise(true);
    removeButton->setToolTip(QStringLiteral("删除这一列"));
    connect(removeButton, &QToolButton::clicked, this, [this] { emit removeRequested(this); });

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(removeButton);

    m_model = new QStringListModel(this);
    m_view = new QListView(this);
    m_view->setObjectName(QStringLiteral("itemsView"));
    m_view->setModel(m_model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 双击或直接敲键盘即可改名 —— 列表内容是这个界面的输入，必须能改
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                            QAbstractItemView::AnyKeyPressed);
    m_view->setUniformItemSizes(true);
    m_view->setToolTip(QStringLiteral("双击可编辑；Enter 确认"));

    auto* addItemButton = new QToolButton(this);
    addItemButton->setObjectName(QStringLiteral("addItemButton"));
    addItemButton->setText(QStringLiteral("＋ 项"));
    addItemButton->setAutoRaise(true);
    addItemButton->setToolTip(QStringLiteral("在末尾新增一项"));
    connect(addItemButton, &QToolButton::clicked, this, &ListSourceColumn::appendItem);

    auto* removeItemButton = new QToolButton(this);
    removeItemButton->setObjectName(QStringLiteral("removeItemButton"));
    removeItemButton->setText(QStringLiteral("－ 项"));
    removeItemButton->setAutoRaise(true);
    removeItemButton->setToolTip(QStringLiteral("删除选中的项"));
    connect(removeItemButton, &QToolButton::clicked, this, &ListSourceColumn::removeSelectedItems);

    auto* footerLayout = new QHBoxLayout;
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->addWidget(addItemButton);
    footerLayout->addWidget(removeItemButton);
    footerLayout->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(headerLayout);
    layout->addWidget(m_view, 1);
    layout->addLayout(footerLayout);

    // 内容变了就通知外面（主窗口据此更新状态栏 / 保留未来的实时预览）
    connect(m_model, &QStringListModel::dataChanged, this, &ListSourceColumn::changed);
    connect(m_model, &QStringListModel::rowsInserted, this, &ListSourceColumn::changed);
    connect(m_model, &QStringListModel::rowsRemoved, this, &ListSourceColumn::changed);
}

batchsmith::core::ListSource ListSourceColumn::source() const {
    return batchsmith::core::ListSource{m_name, m_model->stringList()};
}

void ListSourceColumn::appendItem() {
    const int row = m_model->rowCount();
    m_model->insertRow(row);
    const QModelIndex index = m_model->index(row);
    m_view->setCurrentIndex(index);
    m_view->edit(index);  // 新增后直接进入编辑，少一次点击
}

void ListSourceColumn::removeSelectedItems() {
    const QModelIndexList selection = m_view->selectionModel()->selectedIndexes();
    if (selection.isEmpty()) {
        return;
    }
    // 从后往前删，避免行号在删除过程中前移
    QList<int> rows;
    rows.reserve(selection.size());
    for (const QModelIndex& index : selection) {
        rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (const int row : rows) {
        m_model->removeRow(row);
    }
}
