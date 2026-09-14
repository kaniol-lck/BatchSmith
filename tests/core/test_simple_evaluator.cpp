#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

#include "batchsmith/core/dsl/simple_evaluator.hpp"
#include "batchsmith/core/list/list_source.hpp"

using batchsmith::core::ListSource;
using batchsmith::core::ListSourceList;
using batchsmith::core::dsl::evaluate_simple;
using batchsmith::core::dsl::kMaxExpandedRows;

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

[[nodiscard]] ListSource source(const char16_t* name, const QStringList& items) {
    return ListSource{S(name), items};
}

/// 常用的一组源：list1 = 1,2,3；list2 = a,b,c
[[nodiscard]] ListSourceList sample_sources() {
    return ListSourceList{source(u"list1", {S(u"1"), S(u"2"), S(u"3")}),
                          source(u"list2", {S(u"a"), S(u"b"), S(u"c")})};
}

}  // namespace

TEST_CASE("单个列表区段按项逐行展开") {
    const auto result = evaluate_simple(S(u"$list1$"), sample_sources());
    CHECK(result.ok());
    CHECK(result.rows == QStringList{S(u"1"), S(u"2"), S(u"3")});
}

TEST_CASE("字面段随展开广播到每一行") {
    const auto result = evaluate_simple(S(u"mv $list1$ out"), sample_sources());
    CHECK(result.ok());
    CHECK(result.rows == QStringList{S(u"mv 1 out"), S(u"mv 2 out"), S(u"mv 3 out")});
}

TEST_CASE("两个区段取笛卡尔积，右者为内层循环") {
    const auto result = evaluate_simple(S(u"$list1$-$list2$"), sample_sources());
    CHECK(result.ok());
    REQUIRE(result.rows.size() == 9);

    // 顺序：list1 为外层、list2 为内层 —— 与技术方案 §3.2 的措辞逐字对应
    CHECK(result.rows.first() == S(u"1-a"));
    CHECK(result.rows.at(1) == S(u"1-b"));
    CHECK(result.rows.at(2) == S(u"1-c"));
    CHECK(result.rows.at(3) == S(u"2-a"));
    CHECK(result.rows.last() == S(u"3-c"));
}

TEST_CASE("长度为 1 的列表不触发展开") {
    const ListSourceList sources{source(u"list1", {S(u"x")})};
    const auto result = evaluate_simple(S(u"[$list1$]"), sources);
    CHECK(result.ok());
    CHECK(result.rows == QStringList{S(u"[x]")});
}

TEST_CASE("长度为 0 的列表使该行被跳过") {
    const ListSourceList sources{source(u"list1", {S(u"1"), S(u"2")}), source(u"list2", {})};
    const auto result = evaluate_simple(S(u"$list1$-$list2$"), sources);
    CHECK(result.ok());
    CHECK(result.rows.isEmpty());
}

TEST_CASE("没有区段时只输出字面文本一行") {
    const auto result = evaluate_simple(S(u"固定文本"), sample_sources());
    CHECK(result.ok());
    CHECK(result.rows == QStringList{S(u"固定文本")});
}

TEST_CASE("列表名打错时报错并列出当前可用的名字") {
    const auto result = evaluate_simple(S(u"$list9$"), sample_sources());
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"list9")));
    CHECK(result.error.contains(S(u"list1")));  // 提示里带上现有的名字
    CHECK(result.rows.isEmpty());
}

TEST_CASE("需要 Lua 的表达式明确报错，而不是猜一个语义") {
    // helper、索引、长度运算符都属于 Phase 2 的 DSL 编译器
    for (const char16_t* expression :
         {u"$matrix(list1,list2,'-')$", u"$list1[1]$", u"$#list1$", u"$i$", u"$1+1$"}) {
        const auto result = evaluate_simple(S(expression), sample_sources());
        CHECK_FALSE(result.ok());
        CHECK(result.error.contains(S(u"Phase 2")));
    }
}

TEST_CASE("模板自身的编译错误会原样透出") {
    const auto unpaired = evaluate_simple(S(u"$list1"), sample_sources());
    CHECK_FALSE(unpaired.ok());
    CHECK(unpaired.error.contains(S(u"未配对")));

    const auto empty = evaluate_simple(S(u"$$"), sample_sources());
    CHECK_FALSE(empty.ok());
    CHECK(empty.error.contains(S(u"区段为空")));
}

TEST_CASE("展开行数超过上限时报错，不静默截断") {
    // 400 × 400 = 160000 > kMaxExpandedRows（100000）
    QStringList wide;
    for (int i = 0; i < 400; ++i) {
        wide.append(QString::number(i));
    }
    const ListSourceList sources{source(u"list1", wide), source(u"list2", wide)};

    const auto result = evaluate_simple(S(u"$list1$-$list2$"), sources);
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(QString::number(kMaxExpandedRows)));
    CHECK(result.rows.isEmpty());
}

TEST_CASE("空模板输出 0 行且不算错") {
    const auto result = evaluate_simple(QString(), sample_sources());
    CHECK(result.ok());
    CHECK(result.rows.isEmpty());
}

TEST_CASE("越界取值返回空串而不是崩溃") {
    const ListSource s = source(u"list1", {S(u"a")});
    CHECK(s.at(0) == S(u"a"));
    CHECK(s.at(5).isEmpty());
    CHECK(s.at(-1).isEmpty());
}
