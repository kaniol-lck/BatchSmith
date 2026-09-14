#include <doctest/doctest.h>

#include <QString>

#include "batchsmith/core/dsl/template_scanner.hpp"

using batchsmith::core::dsl::scan_template;
using batchsmith::core::dsl::Segment;

namespace {

/// 与 test_natural_compare.cpp 一致：统一走 u"" 字面量，避免执行字符集上的歧义。
[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 把扫描结果压成便于断言的形式：L:字面 / S:区段。
[[nodiscard]] QStringList shape(const batchsmith::core::dsl::ScanResult& result) {
    QStringList out;
    for (const Segment& segment : result.segments) {
        const QString tag = segment.kind == Segment::Kind::Section ? QStringLiteral("S:")
                                                                   : QStringLiteral("L:");
        out.append(tag + segment.text);
    }
    return out;
}

}  // namespace

TEST_CASE("纯字面模板切成一个字面段") {
    const auto result = scan_template(S(u"没有区段"));
    CHECK(result.ok());
    CHECK(shape(result) == QStringList{S(u"L:没有区段")});
}

TEST_CASE("区段与字面段交替切分") {
    const auto result = scan_template(S(u"mv $list1$ out"));
    CHECK(result.ok());
    CHECK(shape(result) == QStringList{S(u"L:mv "), S(u"S:list1"), S(u"L: out")});

    // 区段在首尾都要能正确切分
    const auto leading = scan_template(S(u"$i$/共$rows$"));
    CHECK(leading.ok());
    CHECK(shape(leading) == QStringList{S(u"S:i"), S(u"L:/共"), S(u"S:rows")});
}

TEST_CASE("区段内首个未转义的 `$` 即结束区段") {
    // 区段里允许出现 `[` `]` 这类字符，不影响切分
    const auto result = scan_template(S(u"$list1[i]$"));
    CHECK(result.ok());
    CHECK(shape(result) == QStringList{S(u"S:list1[i]")});
}

TEST_CASE("`\\$` 转义为字面 `$`，字面段与区段内都生效") {
    const auto literal = scan_template(S(u"价格 100\\$"));
    CHECK(literal.ok());
    CHECK(shape(literal) == QStringList{S(u"L:价格 100$")});

    // 区段里想写字面 `$` 也要用 `\$`：一个区段被切成两半
    const auto inside = scan_template(S(u"$a\\$b$"));
    CHECK(inside.ok());
    CHECK(shape(inside) == QStringList{S(u"S:a$b")});
}

TEST_CASE("除 `\\$` 之外的反斜杠原样保留") {
    // Windows 路径必须原样存活，否则 `C:\temp` 会被吃掉一个字符
    const auto result = scan_template(S(u"C:\\temp\\new$list1$"));
    CHECK(result.ok());
    CHECK(shape(result) == QStringList{S(u"L:C:\\temp\\new"), S(u"S:list1")});

    // 结尾的孤立反斜杠按字面量处理
    const auto trailing = scan_template(S(u"尾巴\\"));
    CHECK(trailing.ok());
    CHECK(shape(trailing) == QStringList{S(u"L:尾巴\\")});
}

TEST_CASE("落单的 `$` 是编译错误，不静默当字面量") {
    const auto result = scan_template(S(u"未闭合 $list1"));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"未配对")));
    CHECK(result.segments.isEmpty());
}

TEST_CASE("空区段是编译错误") {
    // `$$` 只可能是笔误，不该被猜成空串
    const auto result = scan_template(S(u"a$$b"));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"区段为空")));
}

TEST_CASE("空模板合法且没有段") {
    const auto result = scan_template(QString());
    CHECK(result.ok());
    CHECK(result.segments.isEmpty());
}
