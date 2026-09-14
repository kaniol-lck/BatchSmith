/// 文件夹列表源的测试。
///
/// 这一组里最要紧的两条不是"能扫出文件"，而是：
///   1. **自然序** —— `file2` 必须在 `file10` 之前。将来 Plan 的"第 7 行对应哪个文件"
///      靠人眼核对，顺序错了这个安全模型就是假的。
///   2. **错误要说出来** —— 路径不存在必须报错，而不是给一个空列表。空列表在批量
///      操作里意味着"什么都不会发生"，是最难排查的一类问题。

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "batchsmith/core/list/dir_source.hpp"
#include "batchsmith/core/list/list_source.hpp"

#include "doctest/doctest.h"

namespace {

using batchsmith::core::DirQuery;
using batchsmith::core::DirScan;
using batchsmith::core::glob_match;
using batchsmith::core::ListSource;
using batchsmith::core::ListSourceKind;
using batchsmith::core::refresh;
using batchsmith::core::scan_directory;

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 造一个真实目录树。用真实文件系统而不是桩：隐藏项、子目录、前导零这些边界
/// 都是文件系统行为，桩不出来。
///
/// 顶层内容（`*.txt` 共 5 个、`*.mkv` 共 2 个，另有一个隐藏的）：
///   a.mkv  b.txt  file2.txt  file10.txt  前导零01.txt  前导零2.txt  .hidden.mkv
///   sub/c.mkv  sub/nested/d.mkv  sub/.hidden-in-sub.mkv  empty-dir/
class Fixture {
public:
    Fixture() {
        REQUIRE(m_dir.isValid());
        make_file(S(u"a.mkv"));
        make_file(S(u"b.txt"));
        make_file(S(u"file2.txt"));
        make_file(S(u"file10.txt"));
        make_file(S(u"前导零01.txt"));
        make_file(S(u"前导零2.txt"));
        make_file(S(u".hidden.mkv"));
        make_file(S(u"sub/c.mkv"));
        make_file(S(u"sub/nested/d.mkv"));
        make_file(S(u"sub/.hidden-in-sub.mkv"));
        REQUIRE(QDir(m_dir.path()).mkpath(S(u"empty-dir")));
    }

    [[nodiscard]] QString path() const { return m_dir.path(); }

    [[nodiscard]] DirQuery query() const {
        DirQuery q;
        q.path = m_dir.path();
        return q;
    }

private:
    void make_file(const QString& relative) {
        const QString full = QDir(m_dir.path()).filePath(relative);
        REQUIRE(QDir().mkpath(QFileInfo(full).absolutePath()));
        QFile file(full);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("x") == 1);
        file.close();
    }

    QTemporaryDir m_dir;
};

/// 取出条目并**顺带断言扫描成功** —— 失败时把错误原因打出来，
/// 否则一个空列表的断言失败会让人以为是排序问题。
[[nodiscard]] QStringList items_of(const DirScan& scan) {
    CHECK_MESSAGE(scan.ok(), "扫描失败: ", scan.error.toStdString());
    return scan.items;
}

}  // namespace

// ===========================================================================
// glob 匹配 —— 表驱动，一条断言一个形态
// ===========================================================================

TEST_CASE("glob：基本通配") {
    CHECK(glob_match(S(u"*.mkv"), S(u"a.mkv")));
    CHECK(glob_match(S(u"*.mkv"), S(u".mkv")));  // `*` 可以匹配空
    CHECK_FALSE(glob_match(S(u"*.mkv"), S(u"a.txt")));
    CHECK(glob_match(S(u"a.*"), S(u"a.mkv")));
    CHECK(glob_match(S(u"*"), S(u"任意东西")));

    // `?` 恰好一个字符
    CHECK(glob_match(S(u"a?c.txt"), S(u"abc.txt")));
    CHECK_FALSE(glob_match(S(u"a?c.txt"), S(u"ac.txt")));
    CHECK_FALSE(glob_match(S(u"a?c.txt"), S(u"abbc.txt")));
}

TEST_CASE("glob：连续的 * 与回溯") {
    CHECK(glob_match(S(u"a**b"), S(u"ab")));
    CHECK(glob_match(S(u"a**b"), S(u"axxxb")));
    // `*` 必须能回退：模式里前面的 `*` 不能一口吃掉后面要匹配的字面串
    CHECK(glob_match(S(u"*bc"), S(u"abc")));
    CHECK(glob_match(S(u"*b*c"), S(u"abxc")));
    CHECK_FALSE(glob_match(S(u"*b*c"), S(u"abx")));
}

TEST_CASE("glob：字符集") {
    CHECK(glob_match(S(u"[ab].mkv"), S(u"a.mkv")));
    CHECK(glob_match(S(u"[ab].mkv"), S(u"b.mkv")));
    CHECK_FALSE(glob_match(S(u"[ab].mkv"), S(u"c.mkv")));
    CHECK(glob_match(S(u"[a-z].mkv"), S(u"q.mkv")));
    CHECK_FALSE(glob_match(S(u"[!ab].mkv"), S(u"a.mkv")));
    CHECK(glob_match(S(u"[!ab].mkv"), S(u"c.mkv")));
    CHECK(glob_match(S(u"[^ab].mkv"), S(u"c.mkv")));  // `^` 与 `!` 同样表示取反
    CHECK(glob_match(S(u"[]].mkv"), S(u"].mkv")));    // `]` 紧跟 `[` 时是字面字符

    // 未闭合的 `[` 当字面字符 —— 整条模式静默失效比"匹配不到"更难排查
    CHECK(glob_match(S(u"[abc.mkv"), S(u"[abc.mkv")));
}

TEST_CASE("glob：默认大小写不敏感，可显式收紧") {
    // 默认不敏感的理由：Windows/macOS 的文件系统不敏感，而同一份预设在三个平台
    // 必须给出同一个答案。
    CHECK(glob_match(S(u"*.MKV"), S(u"a.mkv")));
    CHECK(glob_match(S(u"*.mkv"), S(u"A.MKV")));
    CHECK(glob_match(S(u"[A-Z].mkv"), S(u"q.mkv")));
    CHECK(glob_match(S(u"ABC.txt"), S(u"abc.txt")));

    // 想要精确匹配时可以要回来
    CHECK_FALSE(glob_match(S(u"*.MKV"), S(u"a.mkv"), Qt::CaseSensitive));
    CHECK(glob_match(S(u"*.MKV"), S(u"a.MKV"), Qt::CaseSensitive));
}

TEST_CASE("glob：不吃目录分隔符") {
    // 这是实现的保证（不只是调用方的约定）：误把整条路径传进来也不会得到意外结果
    CHECK_FALSE(glob_match(S(u"*.mkv"), S(u"sub/c.mkv")));
    CHECK_FALSE(glob_match(S(u"c?mkv"), S(u"c/mkv")));
    CHECK_FALSE(glob_match(S(u"*"), S(u"sub/c.mkv")));
    CHECK(glob_match(S(u"*/*.mkv"), S(u"sub/c.mkv")));  // 显式写了 `/` 才跨层
}

TEST_CASE("glob：空模式与转义") {
    CHECK(glob_match(QStringView(), QStringView()));
    CHECK_FALSE(glob_match(QStringView(), S(u"a")));
    CHECK(glob_match(S(u"\\*"), S(u"*")));  // 反斜杠让可疑字符变回字面
    CHECK_FALSE(glob_match(S(u"\\*"), S(u"a")));
}

// ===========================================================================
// 扫描
// ===========================================================================

TEST_CASE("扫描：默认只列文件、不递归、不含隐藏项") {
    const Fixture fixture;
    const QStringList items = items_of(scan_directory(fixture.query()));

    CHECK(items == QStringList{S(u"a.mkv"),
                               S(u"b.txt"),
                               S(u"file2.txt"),
                               S(u"file10.txt"),
                               S(u"前导零01.txt"),
                               S(u"前导零2.txt")});
}

TEST_CASE("扫描：自然序让 file2 排在 file10 之前") {
    const Fixture fixture;
    const QStringList items = items_of(scan_directory(fixture.query()));

    REQUIRE(items.contains(S(u"file2.txt")));
    REQUIRE(items.contains(S(u"file10.txt")));
    CHECK(items.indexOf(S(u"file2.txt")) < items.indexOf(S(u"file10.txt")));

    // 前导零不参与比较：01 是数值 1，排在 2 之前
    CHECK(items.indexOf(S(u"前导零01.txt")) < items.indexOf(S(u"前导零2.txt")));
}

TEST_CASE("扫描：默认不含子目录条目、也不含子目录里的文件") {
    const Fixture fixture;
    const QStringList items = items_of(scan_directory(fixture.query()));

    CHECK_FALSE(items.contains(S(u".hidden.mkv")));
    CHECK_FALSE(items.contains(S(u"sub")));
    CHECK_FALSE(items.contains(S(u"sub/c.mkv")));
    CHECK_FALSE(items.contains(S(u"empty-dir")));
}

TEST_CASE("扫描：glob 过滤只作用于条目名") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"*.txt");
    const QStringList items = items_of(scan_directory(q));

    REQUIRE(items.size() == 5);
    for (const QString& item : items) {
        CHECK(item.endsWith(S(u".txt")));
    }
}

TEST_CASE("扫描：多个 glob 用 ; 或 , 分隔（任一匹配即可）") {
    const Fixture fixture;
    DirQuery q = fixture.query();

    // a.mkv + 5 个 .txt（.hidden.mkv 仍被隐藏规则挡在外面）
    const qsizetype expected = 6;

    q.filter = S(u"*.mkv;*.txt");
    CHECK(items_of(scan_directory(q)).size() == expected);

    q.filter = S(u"*.mkv,*.txt");
    CHECK(items_of(scan_directory(q)).size() == expected);

    q.filter = S(u"  *.mkv , ; *.txt  ");
    CHECK(items_of(scan_directory(q)).size() == expected);  // 空白与空段被忽略
}

TEST_CASE("扫描：递归后条目带相对路径，且路径同样按自然序") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"*.mkv");
    q.recursive = true;
    const QStringList items = items_of(scan_directory(q));

    CHECK(items.contains(S(u"a.mkv")));
    CHECK(items.contains(S(u"sub/c.mkv")));
    CHECK(items.contains(S(u"sub/nested/d.mkv")));
    CHECK_FALSE(items.contains(S(u"sub/.hidden-in-sub.mkv")));

    // 相对路径用 `/`，跨平台一致（预设因此能在三个平台写成同一个样子）
    for (const QString& item : items) {
        CHECK_FALSE(item.contains(QLatin1Char('\\')));
    }
}

TEST_CASE("扫描：递归时 filter 仍然只看文件名") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"c.mkv");
    q.recursive = true;
    // `sub/c.mkv` 的文件名是 `c.mkv` —— 匹配的是名字，不是路径
    CHECK(items_of(scan_directory(q)) == QStringList{S(u"sub/c.mkv")});
}

TEST_CASE("扫描：include_dirs 把子目录也作为条目") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.include_dirs = true;
    q.filter = QString();  // 不过滤：目录与文件都进来
    const QStringList items = items_of(scan_directory(q));

    CHECK(items.contains(S(u"sub")));
    CHECK(items.contains(S(u"empty-dir")));
    CHECK(items.contains(S(u"a.mkv")));
    CHECK(items.size() == 8);  // 2 个目录 + 6 个文件
}

TEST_CASE("扫描：include_dirs 时过滤对目录名同样生效") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.include_dirs = true;
    q.filter = S(u"*.mkv");
    const QStringList items = items_of(scan_directory(q));

    CHECK(items.contains(S(u"a.mkv")));
    CHECK_FALSE(items.contains(S(u"sub")));  // 目录名不匹配 *.mkv，于是不出现
}

TEST_CASE("扫描：include_hidden 打开后含 . 开头的条目") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.include_hidden = true;
    q.recursive = true;
    q.filter = S(u"*.mkv");
    const QStringList items = items_of(scan_directory(q));

    CHECK(items.contains(S(u".hidden.mkv")));
    CHECK(items.contains(S(u"sub/.hidden-in-sub.mkv")));
}

TEST_CASE("扫描：目录不存在 / 不是目录 / 空路径都要报错，而不是给空列表") {
    const Fixture fixture;

    DirQuery missing = fixture.query();
    missing.path = QDir(fixture.path()).filePath(S(u"根本没有这个目录"));
    const DirScan not_found = scan_directory(missing);
    CHECK_FALSE(not_found.ok());
    CHECK(not_found.error.contains(S(u"不存在")));
    CHECK(not_found.items.isEmpty());

    DirQuery not_a_dir = fixture.query();
    not_a_dir.path = QDir(fixture.path()).filePath(S(u"a.mkv"));
    const DirScan file_as_root = scan_directory(not_a_dir);
    CHECK_FALSE(file_as_root.ok());
    CHECK(file_as_root.error.contains(S(u"不是一个文件夹")));

    DirQuery blank;
    blank.path = S(u"   ");
    CHECK_FALSE(scan_directory(blank).ok());
}

TEST_CASE("扫描：递归不会跟着目录链接绕圈") {
    const Fixture fixture;
    // 造一个指回上层的链接。Windows 上普通用户建目录符号链接需要权限，
    // 建不出来就跳过（CI 的 Linux/macOS 上会真正生效）。
    const QString link = QDir(fixture.path()).filePath(S(u"loop"));
    if (!QFile::link(fixture.path(), link)) {
        return;
    }

    DirQuery q = fixture.query();
    q.recursive = true;
    // 真成环的话这一步不会返回（栈溢出或超时），能走到断言就说明没绕圈
    CHECK(scan_directory(q).ok());
}

// ===========================================================================
// ListSource 的两种来源模式
// ===========================================================================

TEST_CASE("列表源：手输模式不被 refresh 改动") {
    ListSource manual{S(u"list1"), {S(u"a"), S(u"b")}};
    CHECK(manual.kind == ListSourceKind::Manual);

    CHECK(refresh(manual));
    CHECK(manual.items == QStringList{S(u"a"), S(u"b")});
}

TEST_CASE("列表源：绑定文件夹后按规格取数，改了规格再 refresh 生效") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"*.txt");

    QString error;
    ListSource source = ListSource::from_directory(S(u"list1"), q, &error);
    CHECK(error.isEmpty());
    CHECK(source.kind == ListSourceKind::Directory);
    CHECK(source.items.size() == 5);

    source.dir.filter = S(u"*.mkv");
    source.dir.recursive = true;
    CHECK(refresh(source, &error));
    CHECK(error.isEmpty());
    CHECK(source.items.contains(S(u"sub/c.mkv")));
    CHECK_FALSE(source.items.contains(S(u"b.txt")));
}

TEST_CASE("列表源：取数失败时保留上一次的结果，只报错") {
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"*.mkv");

    QString error;
    ListSource source = ListSource::from_directory(S(u"list1"), q, &error);
    REQUIRE(error.isEmpty());
    const QStringList before = source.items;
    REQUIRE_FALSE(before.isEmpty());

    // 路径不可达（U 盘拔了、网络盘掉线）—— 列里应保留上次的数据 + 一条错误，
    // 而不是突然空掉
    source.dir.path = QDir(fixture.path()).filePath(S(u"没有这个目录"));
    QString failure;
    CHECK_FALSE(refresh(source, &failure));
    CHECK_FALSE(failure.isEmpty());
    CHECK(source.items == before);
}

TEST_CASE("列表源：绑定失败时 items 为空，原因带回给调用方") {
    DirQuery q;
    q.path = S(u"/绝对不存在的地方/也没有这个");

    QString error;
    const ListSource source = ListSource::from_directory(S(u"list1"), q, &error);
    CHECK(source.items.isEmpty());
    CHECK_FALSE(error.isEmpty());
}

TEST_CASE("列表源：Directory 模式的取值与手输完全一致") {
    // 来源不同，产出的是同一种东西 —— 下游（求值、将来的 Plan）不该有任何分别
    const Fixture fixture;
    DirQuery q = fixture.query();
    q.filter = S(u"*.txt");

    QString error;
    const ListSource source = ListSource::from_directory(S(u"list1"), q, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(source.size() == 5);

    for (qsizetype index = 0; index < source.size(); ++index) {
        CHECK(source.value_at(index) == source.items.at(index));
    }
    CHECK(source.value_at(source.size()).isEmpty());  // 越界给空串（Empty 缺省）
    CHECK(source.value_at(-1).isEmpty());
}
