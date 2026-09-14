/// 列表列（来源模式切换）的离屏测试。
///
/// 这一组要钉住的不是"控件在不在"，而是**切换模式后行为仍然对**：
///   * 换来源不会换列名 —— 否则表达式里的 `$list1[i]$` 会静默指向别的东西；
///   * 文件夹模式下列表只读，手输模式可编辑；
///   * 取数失败时保留上一次的结果，只报错；
///   * 拖一个文件夹进来与点「…」选一个，走的是同一条路。

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUrl>

#include "batchsmith/core/list/list_source.hpp"
#include "table/ListSourceColumn.h"

#include "doctest/doctest.h"

namespace {

using batchsmith::core::ListSource;
using batchsmith::core::ListSourceKind;

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 一个真实的小文件夹：3 个 mkv + 1 个 txt + 1 个子目录（含 1 个 mkv）
class Folder {
public:
    Folder() {
        REQUIRE(m_dir.isValid());
        write(S(u"番剧 第1话.mkv"));
        write(S(u"番剧 第2话.mkv"));
        write(S(u"番剧 第10话.mkv"));
        write(S(u"读我.txt"));
        write(S(u"第01话/正片.mkv"));
    }

    [[nodiscard]] QString path() const { return m_dir.path(); }

private:
    void write(const QString& relative) {
        const QString full = QDir(m_dir.path()).filePath(relative);
        REQUIRE(QDir().mkpath(QFileInfo(full).absolutePath()));
        QFile file(full);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("x") == 1);
        file.close();
    }

    QTemporaryDir m_dir;
};

/// 造一个"把这条路径拖进来"的拖放事件，并真的发给这一列。
/// 文件夹与文件走的是同一条路 —— 区别只在于该不该被接受（见下面的用例）。
///
/// 先送 DragEnter 再送 Drop：真实拖放就是这个次序，`dragEnterEvent` 里
/// 接受了动作之后，Qt 才会把 Drop 交给同一个 widget。
void drop_path(ListSourceColumn* column, const QString& path) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(column, &enter);

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(column, &drop);
}

}  // namespace

TEST_CASE("列表列：默认是手输模式") {
    ListSourceColumn column(S(u"list1"));

    CHECK(column.kind() == ListSourceKind::Manual);
    CHECK(column.kindCombo()->currentIndex() == 0);
    CHECK(column.kindCombo()->currentText() == S(u"手输"));

    // 手输模式：增删按钮在、文件夹控件不在
    CHECK(column.findChild<QWidget*>(QStringLiteral("manualFooter"))->isVisibleTo(&column));
    CHECK_FALSE(column.pathEdit()->isVisibleTo(&column));
    CHECK_FALSE(column.filterEdit()->isVisibleTo(&column));

    // 可编辑
    CHECK(column.itemsView()->editTriggers() != QAbstractItemView::NoEditTriggers);

    auto* kindCombo = column.findChild<QComboBox*>(QStringLiteral("sourceKindCombo"));
    REQUIRE(kindCombo != nullptr);
    CHECK(kindCombo->count() == 2);
    CHECK(kindCombo->itemText(1) == S(u"文件夹"));
}

TEST_CASE("列表列：切到文件夹模式后，控件在上、列表只读") {
    ListSourceColumn column(S(u"list1"));

    column.setKind(ListSourceKind::Directory);

    CHECK(column.kind() == ListSourceKind::Directory);
    CHECK(column.pathEdit()->isVisibleTo(&column));
    CHECK(column.filterEdit()->isVisibleTo(&column));
    CHECK_FALSE(column.findChild<QWidget*>(QStringLiteral("manualFooter"))->isVisibleTo(&column));

    // 只读：内容由规格决定，手改会被下一次取数覆盖
    CHECK(column.itemsView()->editTriggers() == QAbstractItemView::NoEditTriggers);

    // 还没绑定时说清楚，而不是留一个空列表让人猜
    CHECK(column.statusLabel()->text().contains(S(u"还没绑定")));
    CHECK(column.items().isEmpty());
}

TEST_CASE("列表列：绑定文件夹后按自然序读入条目") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));

    column.bindDirectory(folder.path());

    CHECK(column.kind() == ListSourceKind::Directory);
    CHECK(column.items() == QStringList{S(u"番剧 第1话.mkv"),
                                        S(u"番剧 第2话.mkv"),
                                        S(u"番剧 第10话.mkv"),
                                        S(u"读我.txt")});
    CHECK(column.statusLabel()->text().contains(S(u"已读入 4 项")));

    // 路径显示成用户熟悉的样子（Windows 上是反斜杠），但规格里统一是 `/`
    CHECK(column.source().dir.path.contains(S(u"/")));
    CHECK_FALSE(column.source().dir.path.contains(S(u"\\")));
}

TEST_CASE("列表列：过滤生效，且只作用于条目名") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());

    column.filterEdit()->setText(S(u"*.mkv"));
    column.refreshButton()->click();

    CHECK(column.items() ==
          QStringList{S(u"番剧 第1话.mkv"), S(u"番剧 第2话.mkv"), S(u"番剧 第10话.mkv")});
    CHECK(column.statusLabel()->text().contains(S(u"已读入 3 项")));

    // 空过滤 = 全部
    column.filterEdit()->setText(QString());
    column.refreshButton()->click();
    CHECK(column.items().size() == 4);
}

TEST_CASE("列表列：递归开关把子目录里的文件也带进来") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());

    column.filterEdit()->setText(S(u"*.mkv"));
    column.refreshButton()->click();
    CHECK_FALSE(column.items().contains(S(u"第01话/正片.mkv")));

    column.recursiveCheck()->setChecked(true);
    column.refreshButton()->click();
    CHECK(column.items().contains(S(u"第01话/正片.mkv")));
    CHECK(column.statusLabel()->text().contains(S(u"已读入 4 项")));

    // 相对路径用 `/`（跨平台一致）
    for (const QString& item : column.items()) {
        CHECK_FALSE(item.contains(S(u"\\")));
    }
}

TEST_CASE("列表列：含目录开关把子目录本身也算条目") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());

    column.filterEdit()->setText(QString());  // 不过滤，否则目录名多半不匹配
    column.dirsCheck()->setChecked(true);
    column.refreshButton()->click();

    CHECK(column.items().contains(S(u"第01话")));
}

TEST_CASE("列表列：取数失败时保留上一次的结果，并在状态行报错") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());
    const QStringList before = column.items();
    REQUIRE_FALSE(before.isEmpty());

    // 路径临时不可达（U 盘拔了、网络盘掉线）
    column.pathEdit()->setText(QDir(folder.path()).filePath(S(u"没有这个目录")));
    column.refreshButton()->click();

    CHECK(column.items() == before);  // 内容不动
    CHECK(column.statusLabel()->text().contains(S(u"不存在")));
}

TEST_CASE("列表列：拖一个文件夹进来与点「…」选一个，效果相同") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    REQUIRE(column.kind() == ListSourceKind::Manual);

    drop_path(&column, folder.path());

    CHECK(column.kind() == ListSourceKind::Directory);
    CHECK(column.items().size() == 4);
    CHECK(column.pathEdit()->text() == QDir::toNativeSeparators(folder.path()));
}

TEST_CASE("列表列：拖一个文件过来不绑定（只有文件夹可以）") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));

    drop_path(&column, QDir(folder.path()).filePath(S(u"读我.txt")));

    CHECK(column.kind() == ListSourceKind::Manual);
    CHECK(column.pathEdit()->text().isEmpty());
}

TEST_CASE("列表列：换来源不换列名（表达式里的 $list1$ 不会指向别处）") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));

    CHECK(column.name() == S(u"list1"));
    column.bindDirectory(folder.path());
    CHECK(column.name() == S(u"list1"));
    CHECK(column.source().name == S(u"list1"));

    column.setKind(ListSourceKind::Manual);
    CHECK(column.name() == S(u"list1"));
    CHECK(column.source().name == S(u"list1"));
}

TEST_CASE("列表列：切回手输时保留读到的内容，接着手改即可") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());
    const QStringList scanned = column.items();

    column.setKind(ListSourceKind::Manual);

    CHECK(column.items() == scanned);  // 保留，作为手改的起点
    CHECK(column.itemsView()->editTriggers() != QAbstractItemView::NoEditTriggers);
    CHECK(column.pathEdit()->isVisibleTo(&column) == false);
}

TEST_CASE("列表列：导出的规格带上全部的取数参数") {
    const Folder folder;
    ListSourceColumn column(S(u"list1"));
    column.bindDirectory(folder.path());
    column.filterEdit()->setText(S(u"*.mkv;*.mp4"));
    column.recursiveCheck()->setChecked(true);
    column.dirsCheck()->setChecked(true);

    const ListSource source = column.source();

    CHECK(source.name == S(u"list1"));
    CHECK(source.kind == ListSourceKind::Directory);
    CHECK(source.dir.path == QDir::fromNativeSeparators(folder.path()));
    CHECK(source.dir.filter == S(u"*.mkv;*.mp4"));
    CHECK(source.dir.recursive);
    CHECK(source.dir.include_dirs);
    CHECK_FALSE(source.dir.include_hidden);  // 界面暂未暴露，保持默认

    // 手输模式下不带走文件夹规格 —— 免得下游以为这一列还绑着谁
    column.setKind(ListSourceKind::Manual);
    const ListSource manual = column.source();
    CHECK(manual.kind == ListSourceKind::Manual);
    CHECK(manual.dir.path.isEmpty());
}

TEST_CASE("列表列：手输模式下刷新按钮不做任何事") {
    ListSourceColumn column(S(u"list1"));
    column.itemsView()->model()->insertRow(0);
    column.itemsView()->model()->setData(column.itemsView()->model()->index(0, 0), S(u"手输项"));
    REQUIRE(column.items() == QStringList{S(u"手输项")});

    column.refreshDirectory();  // 手输模式下不该动内容

    CHECK(column.items() == QStringList{S(u"手输项")});
}
