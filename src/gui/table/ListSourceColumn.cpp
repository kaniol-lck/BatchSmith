#include "table/ListSourceColumn.h"

#include <algorithm>
#include <functional>
#include <utility>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

using batchsmith::core::ListSourceKind;

/// 模式下拉里的次序。刻意不直接拿 core 的枚举值当下标：下拉的次序是界面上的事，
/// 将来插入新模式时不该牵动 core 的语义，所以这里显式映射。
constexpr int kManualIndex = 0;
constexpr int kDirectoryIndex = 1;

/// 列宽。比"只有列表"时宽一些：文件夹模式下要放得下一行路径输入。
constexpr int kColumnWidth = 218;

/// 改动规格后延迟取数的时长。连续敲路径不该每个字符都扫一次盘。
constexpr int kRefreshDelayMs = 250;

}  // namespace

ListSourceColumn::ListSourceColumn(QString name, QWidget* parent)
    : QWidget(parent), m_name(std::move(name)) {
    setFixedWidth(kColumnWidth);
    // 用列表名做对象名：既便于离屏 GUI 测试按名字找到某一列（findChild("list2")），
    // 也让 Qt 的对象树在调试时一眼能读。
    setObjectName(m_name);
    setAcceptDrops(true);  // 把文件夹拖到这一列上就完成绑定

    // ---- 标题行：列表名 + 来源模式 + 删除 ----

    m_titleLabel = new QLabel(m_name, this);
    m_titleLabel->setObjectName(QStringLiteral("titleLabel"));
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_kindCombo = new QComboBox(this);
    m_kindCombo->setObjectName(QStringLiteral("sourceKindCombo"));
    m_kindCombo->addItem(QStringLiteral("手输"));
    m_kindCombo->addItem(QStringLiteral("文件夹"));
    m_kindCombo->setToolTip(QStringLiteral("这一列的来源：手输条目，或绑定一个文件夹\n"
                                           "（也可以直接把文件夹拖到这一列上）"));
    m_kindCombo->setFixedHeight(22);

    auto* removeButton = new QToolButton(this);
    removeButton->setObjectName(QStringLiteral("removeColumnButton"));
    removeButton->setText(QStringLiteral("×"));
    removeButton->setAutoRaise(true);
    removeButton->setToolTip(QStringLiteral("删除这一列"));
    connect(removeButton, &QToolButton::clicked, this, [this] { emit removeRequested(this); });

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_kindCombo);
    headerLayout->addWidget(removeButton);

    // ---- 文件夹模式的控制区（规格）----

    m_dirControls = new QWidget(this);
    m_dirControls->setObjectName(QStringLiteral("dirControls"));

    m_pathEdit = new QLineEdit(m_dirControls);
    m_pathEdit->setObjectName(QStringLiteral("dirPathEdit"));
    m_pathEdit->setPlaceholderText(QStringLiteral("拖入文件夹，或点 …"));
    m_pathEdit->setToolTip(QStringLiteral("绑定的文件夹（也可以拖一个文件夹到这一列上）"));

    m_browseButton = new QToolButton(m_dirControls);
    m_browseButton->setObjectName(QStringLiteral("browseDirButton"));
    m_browseButton->setText(QStringLiteral("…"));
    m_browseButton->setAutoRaise(true);
    m_browseButton->setToolTip(QStringLiteral("选择一个文件夹"));
    connect(m_browseButton, &QToolButton::clicked, this, &ListSourceColumn::chooseDirectory);

    auto* pathLayout = new QHBoxLayout;
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(2);
    pathLayout->addWidget(m_pathEdit, 1);
    pathLayout->addWidget(m_browseButton);

    m_filterEdit = new QLineEdit(m_dirControls);
    m_filterEdit->setObjectName(QStringLiteral("dirFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("过滤，如 *.mkv（留空=全部）"));
    m_filterEdit->setToolTip(QStringLiteral("按条目名过滤；多个用 ; 或 , 分隔\n"
                                            "大小写不敏感，只匹配文件名、不跨目录"));

    m_recursiveCheck = new QCheckBox(QStringLiteral("递归"), m_dirControls);
    m_recursiveCheck->setObjectName(QStringLiteral("dirRecursiveCheck"));
    m_recursiveCheck->setToolTip(QStringLiteral("连子目录里的文件一起取\n"
                                                "（条目会变成 第01话/正片.mkv 这样的相对路径）"));

    m_dirsCheck = new QCheckBox(QStringLiteral("含目录"), m_dirControls);
    m_dirsCheck->setObjectName(QStringLiteral("dirIncludeDirsCheck"));
    m_dirsCheck->setToolTip(QStringLiteral("把子目录本身也算作条目（一集一个文件夹时用）"));

    auto* switchLayout = new QHBoxLayout;
    switchLayout->setContentsMargins(0, 0, 0, 0);
    switchLayout->setSpacing(8);
    switchLayout->addWidget(m_recursiveCheck);
    switchLayout->addWidget(m_dirsCheck);
    switchLayout->addStretch();

    m_refreshButton = new QToolButton(m_dirControls);
    m_refreshButton->setObjectName(QStringLiteral("dirRefreshButton"));
    m_refreshButton->setText(QStringLiteral("刷新"));
    m_refreshButton->setAutoRaise(true);
    m_refreshButton->setToolTip(QStringLiteral("按当前规格重新读一次文件夹"));
    connect(m_refreshButton, &QToolButton::clicked, this, &ListSourceColumn::refreshDirectory);

    m_statusLabel = new QLabel(m_dirControls);
    m_statusLabel->setObjectName(QStringLiteral("dirStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto* statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(4);
    statusLayout->addWidget(m_refreshButton);
    statusLayout->addWidget(m_statusLabel, 1);

    auto* dirLayout = new QVBoxLayout(m_dirControls);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    dirLayout->setSpacing(3);
    dirLayout->addLayout(pathLayout);
    dirLayout->addWidget(m_filterEdit);
    dirLayout->addLayout(switchLayout);
    dirLayout->addLayout(statusLayout);

    // ---- 列表（两种模式共用）----

    m_model = new QStringListModel(this);
    m_view = new QListView(this);
    m_view->setObjectName(QStringLiteral("itemsView"));
    m_view->setModel(m_model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // 显式关掉视图自己的拖放：让拖到列表上的文件夹**冒泡到整列**，
    // 于是"拖到哪都能绑定"，而不是只有标题那一小条能接。
    m_view->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_view->setUniformItemSizes(true);

    // ---- 手输模式的按钮行 ----

    m_manualFooter = new QWidget(this);
    m_manualFooter->setObjectName(QStringLiteral("manualFooter"));

    auto* addItemButton = new QToolButton(m_manualFooter);
    addItemButton->setObjectName(QStringLiteral("addItemButton"));
    addItemButton->setText(QStringLiteral("＋ 项"));
    addItemButton->setAutoRaise(true);
    addItemButton->setToolTip(QStringLiteral("在末尾新增一项"));
    connect(addItemButton, &QToolButton::clicked, this, &ListSourceColumn::appendItem);

    auto* removeItemButton = new QToolButton(m_manualFooter);
    removeItemButton->setObjectName(QStringLiteral("removeItemButton"));
    removeItemButton->setText(QStringLiteral("－ 项"));
    removeItemButton->setAutoRaise(true);
    removeItemButton->setToolTip(QStringLiteral("删除选中的项"));
    connect(removeItemButton, &QToolButton::clicked, this, &ListSourceColumn::removeSelectedItems);

    auto* footerLayout = new QHBoxLayout(m_manualFooter);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->addWidget(addItemButton);
    footerLayout->addWidget(removeItemButton);
    footerLayout->addStretch();

    // ---- 总布局 ----

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(headerLayout);
    layout->addWidget(m_dirControls);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_manualFooter);

    // ---- 取数时机 ----

    // 防抖：连续改动只在停顿后扫一次盘
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(kRefreshDelayMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &ListSourceColumn::refreshDirectory);

    const auto scheduleRefresh = [this] { m_refreshTimer->start(); };

    // 路径与过滤按"编辑完成"取数（回车或失焦），而不是每敲一个字
    connect(m_pathEdit, &QLineEdit::editingFinished, this, scheduleRefresh);
    connect(m_filterEdit, &QLineEdit::editingFinished, this, scheduleRefresh);
    connect(m_recursiveCheck, &QCheckBox::toggled, this, scheduleRefresh);
    connect(m_dirsCheck, &QCheckBox::toggled, this, scheduleRefresh);

    connect(m_kindCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyKindToUi();
        if (kind() == ListSourceKind::Directory) {
            refreshDirectory();
        } else {
            emit changed();
        }
    });

    // 内容变了就通知外面（主窗口据此更新状态栏 / 保留未来的实时预览）
    connect(m_model, &QStringListModel::dataChanged, this, &ListSourceColumn::changed);
    connect(m_model, &QStringListModel::rowsInserted, this, &ListSourceColumn::changed);
    connect(m_model, &QStringListModel::rowsRemoved, this, &ListSourceColumn::changed);

    applyKindToUi();
}

ListSourceKind ListSourceColumn::kind() const {
    return m_kindCombo->currentIndex() == kDirectoryIndex ? ListSourceKind::Directory
                                                          : ListSourceKind::Manual;
}

QStringList ListSourceColumn::items() const {
    return m_model->stringList();
}

batchsmith::core::ListSource ListSourceColumn::source() const {
    batchsmith::core::ListSource result;
    result.name = m_name;
    result.kind = kind();
    result.items = m_model->stringList();
    if (result.kind == ListSourceKind::Directory) {
        // 规格一起带走：将来 Plan 需要知道"第 7 行对应哪个根目录下的哪个文件"。
        // 路径统一存 `/` 形式（界面上显示本地分隔符只是为了好看）——
        // 与条目用 `/` 分隔同理，同一份规格在三个平台上写成同一个样子。
        result.dir.path = QDir::fromNativeSeparators(m_pathEdit->text().trimmed());
        result.dir.filter = m_filterEdit->text();
        result.dir.recursive = m_recursiveCheck->isChecked();
        result.dir.include_dirs = m_dirsCheck->isChecked();
    }
    return result;
}

void ListSourceColumn::applyKindToUi() {
    const bool is_directory = kind() == ListSourceKind::Directory;

    m_dirControls->setVisible(is_directory);
    m_manualFooter->setVisible(!is_directory);

    // 文件夹模式下列表只读：内容由规格决定，手改会被下一次取数覆盖 ——
    // 与其让用户改了又丢，不如明确"要手改就切到「手输」"。
    m_view->setEditTriggers(is_directory ? QAbstractItemView::NoEditTriggers
                                         : (QAbstractItemView::DoubleClicked |
                                            QAbstractItemView::EditKeyPressed |
                                            QAbstractItemView::AnyKeyPressed));
    m_view->setToolTip(is_directory ? QStringLiteral("内容来自绑定的文件夹；想手改请切到「手输」")
                                    : QStringLiteral("双击可编辑；Enter 确认"));

    if (is_directory && m_pathEdit->text().trimmed().isEmpty()) {
        // 从手输切过来时先清掉原有的条目 —— 否则"这一列显示着手输的内容"
        // 与"模式是文件夹"长期矛盾，用户会以为绑定没生效。
        m_model->setStringList({});
        updateStatus(QStringLiteral("还没绑定文件夹"), false);
    }
}

void ListSourceColumn::setKind(ListSourceKind kind) {
    const int index = (kind == ListSourceKind::Directory) ? kDirectoryIndex : kManualIndex;
    if (m_kindCombo->currentIndex() == index) {
        applyKindToUi();
        return;
    }
    // 改下拉就会走到 currentIndexChanged 里那一套（同步界面 + 取数）
    m_kindCombo->setCurrentIndex(index);
}

void ListSourceColumn::applySpec(const batchsmith::core::ListSource& source) {
    const bool is_directory = source.kind == ListSourceKind::Directory;

    // 先把规格填进控件，再切模式 —— 切模式那一步就会按规格取数
    if (is_directory) {
        m_pathEdit->setText(source.dir.path.isEmpty() ? QString()
                                                      : QDir::toNativeSeparators(source.dir.path));
        m_filterEdit->setText(source.dir.filter);
        m_recursiveCheck->setChecked(source.dir.recursive);
        m_dirsCheck->setChecked(source.dir.include_dirs);
    }

    m_kindCombo->setCurrentIndex(is_directory ? kDirectoryIndex : kManualIndex);

    if (is_directory) {
        // 重新读一次：预设里存的是"从哪取"而不是"取到了什么"，
        // 照抄预设里的 items 会让人看到上次打开时的旧内容
        refreshDirectory();
    } else {
        m_model->setStringList(source.items);
    }
}

void ListSourceColumn::bindDirectory(const QString& path) {
    if (path.trimmed().isEmpty()) {
        return;
    }
    m_pathEdit->setText(QDir::toNativeSeparators(path));
    m_kindCombo->setCurrentIndex(kDirectoryIndex);
    // 已经在文件夹模式时 setCurrentIndex 不发信号，所以显式取一次数
    refreshDirectory();
}

void ListSourceColumn::refreshDirectory() {
    if (kind() != ListSourceKind::Directory) {
        return;
    }

    batchsmith::core::DirQuery query;
    query.path = QDir::fromNativeSeparators(m_pathEdit->text().trimmed());
    query.filter = m_filterEdit->text();
    query.recursive = m_recursiveCheck->isChecked();
    query.include_dirs = m_dirsCheck->isChecked();

    if (query.path.isEmpty()) {
        m_model->setStringList({});
        updateStatus(QStringLiteral("还没绑定文件夹"), false);
        emit changed();
        return;
    }

    const batchsmith::core::DirScan scan = batchsmith::core::scan_directory(query);
    if (!scan.ok()) {
        // 刻意不动列表：路径暂时不可达时，保留上一次的结果比清空更有用
        updateStatus(scan.error, true);
        return;
    }

    m_model->setStringList(scan.items);
    if (scan.items.isEmpty()) {
        updateStatus(QStringLiteral("没有符合条件的条目"), false);
    } else {
        updateStatus(QStringLiteral("已读入 %1 项（自然序）").arg(scan.items.size()), false);
    }
    emit changed();
}

void ListSourceColumn::updateStatus(const QString& text, bool is_error) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(is_error ? QStringLiteral("color: #c0392b;")
                                          : QStringLiteral("color: palette(mid);"));
    m_statusLabel->setToolTip(is_error ? text : QString());
}

void ListSourceColumn::chooseDirectory() {
    const QString initial = m_pathEdit->text().trimmed();
    const QString chosen =
            QFileDialog::getExistingDirectory(this,
                                              QStringLiteral("选择要绑定的文件夹"),
                                              initial.isEmpty() ? QDir::homePath() : initial);
    if (chosen.isEmpty()) {
        return;  // 用户取消
    }
    bindDirectory(chosen);
}

QString ListSourceColumn::directoryFromMimeData(const QMimeData* mime) {
    if (mime == nullptr || !mime->hasUrls()) {
        return {};
    }
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (info.isDir()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

void ListSourceColumn::dragEnterEvent(QDragEnterEvent* event) {
    if (directoryFromMimeData(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ListSourceColumn::dropEvent(QDropEvent* event) {
    const QString directory = directoryFromMimeData(event->mimeData());
    if (directory.isEmpty()) {
        event->ignore();
        return;
    }
    bindDirectory(directory);
    event->acceptProposedAction();
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
