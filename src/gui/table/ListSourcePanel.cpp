#include "table/ListSourcePanel.h"

#include <utility>

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "table/ListSourceColumn.h"

ListSourcePanel::ListSourcePanel(QWidget* parent) : QWidget(parent) {
    // 对象名供离屏 GUI 测试定位（见 tests/gui/test_main_window.cpp）
    setObjectName(QStringLiteral("listSourcePanel"));

    auto* addButton = new QToolButton(this);
    addButton->setObjectName(QStringLiteral("addColumnButton"));
    addButton->setText(QStringLiteral("＋ 添加列表"));
    addButton->setToolTip(QStringLiteral("在右侧新增一个列表列"));
    connect(addButton, &QToolButton::clicked, this, &ListSourcePanel::addColumn);

    auto* hint = new QLabel(QStringLiteral("每列是一个列表源：手输条目，或绑定文件夹"
                                           "（把文件夹拖到列上即可）。表达式里用 $list1$ 引用。"),
                            this);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(addButton);
    headerLayout->addWidget(hint);
    headerLayout->addStretch();

    // 列都固定宽度；widgetResizable(true) 让容器至少撑到布局的最小宽度，
    // 超出视口时水平滚动条自动出现，而高度始终与视口一致（列因此能竖向撑满）。
    auto* container = new QWidget;
    container->setAutoFillBackground(false);

    m_columnLayout = new QHBoxLayout(container);
    m_columnLayout->setContentsMargins(0, 0, 0, 0);
    m_columnLayout->setSpacing(8);
    m_columnLayout->addStretch();

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidget(container);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(headerLayout);
    layout->addWidget(m_scrollArea, 1);
}

QString ListSourcePanel::nextName() const {
    QStringList used;
    used.reserve(m_columns.size());
    for (const ListSourceColumn* column : m_columns) {
        used.append(column->name());
    }
    for (int index = 1;; ++index) {
        const QString candidate = QStringLiteral("list%1").arg(index);
        if (!used.contains(candidate)) {
            return candidate;
        }
    }
}

void ListSourcePanel::addColumn() {
    auto* column = new ListSourceColumn(nextName(), this);
    connect(column, &ListSourceColumn::removeRequested, this, &ListSourcePanel::removeColumn);
    connect(column, &ListSourceColumn::changed, this, &ListSourcePanel::sourcesChanged);

    // 插在末尾的伸缩项之前，保持列从左到右按添加顺序排列
    m_columnLayout->insertWidget(m_columnLayout->count() - 1, column);
    m_columns.append(column);

    emit sourcesChanged();
}

void ListSourcePanel::removeColumn(ListSourceColumn* column) {
    if (!m_columns.removeOne(column)) {
        return;
    }
    m_columnLayout->removeWidget(column);
    column->deleteLater();
    emit sourcesChanged();
}

batchsmith::core::ListSourceList ListSourcePanel::sources() const {
    batchsmith::core::ListSourceList sources;
    sources.reserve(m_columns.size());
    for (const ListSourceColumn* column : m_columns) {
        sources.append(column->source());
    }
    return sources;
}

void ListSourcePanel::setSources(const batchsmith::core::ListSourceList& sources) {
    for (ListSourceColumn* column : std::as_const(m_columns)) {
        m_columnLayout->removeWidget(column);
        // 先摘掉父子关系再 deleteLater()。
        //
        // 删除是**延后**的（在列自己的槽里立刻 delete 不安全），不摘的话那些
        // "等着被删的旧列"仍挂在对象树上 —— 紧接着按名字找 `list1` 会先找到旧列，
        // 于是刚装好的内容看起来像没生效。（测试当场抓到了这一条。）
        column->setParent(nullptr);
        column->deleteLater();
    }
    m_columns.clear();

    for (const batchsmith::core::ListSource& source : sources) {
        auto* column = new ListSourceColumn(source.name, this);
        connect(column, &ListSourceColumn::removeRequested, this, &ListSourcePanel::removeColumn);
        connect(column, &ListSourceColumn::changed, this, &ListSourcePanel::sourcesChanged);

        m_columnLayout->insertWidget(m_columnLayout->count() - 1, column);
        m_columns.append(column);
        column->applySpec(source);
    }

    emit sourcesChanged();
}
