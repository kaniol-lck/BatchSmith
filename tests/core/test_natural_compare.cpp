#include <doctest/doctest.h>

#include <algorithm>

#include <QString>
#include <QStringList>

#include "batchsmith/core/text/natural_compare.hpp"

using batchsmith::core::natural_compare;
using batchsmith::core::natural_less;

namespace {

/// 测试里中英文混排的字面量很多，统一用 u"" 字面量（明确是 UTF-16，
/// 不受编译器默认执行字符集影响），再显式转成 QString。
/// 不依赖 QString 对 const char16_t* 的隐式转换，避免构造重载上的歧义。
[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

}  // namespace

TEST_CASE("数字段按数值比较，而不是字典序") {
    // 这是 M0 的验收项：字典序会把 file10 排到 file2 前面
    CHECK(natural_less(S(u"file2"), S(u"file10")));
    CHECK(natural_less(S(u"file9"), S(u"file10")));
    CHECK(natural_less(S(u"file10"), S(u"file100")));
    CHECK(natural_less(S(u"file1"), S(u"file2")));

    CHECK(natural_compare(S(u"file2"), S(u"file2")) == 0);
    CHECK(natural_compare(S(u"file10"), S(u"file2")) > 0);
}

TEST_CASE("中文文件名同样按数字分段") {
    CHECK(natural_less(S(u"文件2"), S(u"文件10")));
    CHECK(natural_less(S(u"第1章"), S(u"第2章")));
    CHECK(natural_less(S(u"第2章"), S(u"第10章")));
    CHECK(natural_compare(S(u"文件1"), S(u"文件1")) == 0);
}

TEST_CASE("前导零") {
    // 数值相同的情况下用前导零个数兜底：**多的排后面**。
    // 没有这一步 "01" 与 "1" 会被判为相等，排序就不再是全序，
    // 而 std::sort 要求严格弱序。方向与 strnatcmp / 资源管理器一致。
    CHECK(natural_compare(S(u"1"), S(u"01")) < 0);
    CHECK(natural_compare(S(u"01"), S(u"1")) > 0);
    CHECK(natural_compare(S(u"0"), S(u"000")) < 0);

    CHECK(natural_compare(S(u"a1"), S(u"a01")) < 0);
    CHECK(natural_compare(S(u"a1b"), S(u"a001b")) < 0);

    // 前导零不影响数值本身的大小关系
    CHECK(natural_less(S(u"a009"), S(u"a10")));
    CHECK(natural_less(S(u"a009"), S(u"a00010")));
}

TEST_CASE("边界情况") {
    CHECK(natural_compare(S(u""), S(u"")) == 0);
    CHECK(natural_compare(S(u""), S(u"a")) < 0);
    CHECK(natural_compare(S(u"a"), S(u"")) > 0);

    // 前缀更短的排前面
    CHECK(natural_less(S(u"file"), S(u"file1")));
    CHECK(natural_less(S(u"file1"), S(u"file1a")));

    CHECK(natural_compare(S(u"abcabcabc"), S(u"abcabcabc")) == 0);
}

TEST_CASE("数字段排在非数字段之前") {
    CHECK(natural_compare(S(u"1a"), S(u"a1")) < 0);
    CHECK(natural_compare(S(u"a1"), S(u"1a")) > 0);
}

TEST_CASE("大小写敏感性可控") {
    // 默认按 UTF-16 码元序：'B'(0x42) 在 'a'(0x61) 之前
    CHECK(natural_compare(S(u"a"), S(u"B")) > 0);

    CHECK(natural_compare(S(u"a"), S(u"A"), Qt::CaseInsensitive) == 0);
    CHECK(natural_compare(S(u"a"), S(u"B"), Qt::CaseInsensitive) < 0);

    // 大小写开关不应影响数字段的比较
    CHECK(natural_compare(S(u"file10"), S(u"FILE9"), Qt::CaseInsensitive) > 0);
}

TEST_CASE("整体排序结果符合预期") {
    QStringList items{
            S(u"file10"),
            S(u"file2"),
            S(u"文件10"),
            S(u"文件2"),
            S(u"file1"),
            S(u"文件1"),
            S(u"file"),
    };

    std::sort(items.begin(), items.end(), [](const QString& a, const QString& b) {
        return natural_less(a, b);
    });

    // 'f'(0x66) 在 '文'(0x6587) 之前，所以 file* 整组排在 文件* 之前
    const QStringList expected{
            S(u"file"),
            S(u"file1"),
            S(u"file2"),
            S(u"file10"),
            S(u"文件1"),
            S(u"文件2"),
            S(u"文件10"),
    };

    CHECK(items == expected);
}

TEST_CASE("比较器满足严格弱序") {
    // std::sort 的正确性依赖这一点；顺带覆盖空串、纯数字与前导零的交叉情况
    const QStringList items{
            S(u"a"),
            S(u"a1"),
            S(u"a01"),
            S(u"a10"),
            S(u"a2"),
            S(u""),
            S(u"1"),
            S(u"01"),
            S(u"A"),
            S(u"b"),
    };

    for (const QString& x : items) {
        CHECK(natural_compare(x, x) == 0);
        for (const QString& y : items) {
            const int xy = natural_compare(x, y);
            const int yx = natural_compare(y, x);

            // 反对称性：xy 与 yx 必须反号（或同为 0）
            CHECK((xy == 0) == (yx == 0));
            CHECK((xy < 0) == (yx > 0));
        }
    }
}
