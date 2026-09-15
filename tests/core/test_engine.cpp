#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/sandbox/sandbox.hpp"

using batchsmith::core::ListPadding;
using batchsmith::core::ListSource;
using batchsmith::core::ListSourceList;
using batchsmith::core::dsl::evaluate_template;

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 构想书「使用示例」一节的输入列表：list1 = 1,2,3；list2 = a,b,c
[[nodiscard]] ListSourceList sample_sources() {
    return ListSourceList{
            ListSource{S(u"list1"), {S(u"1"), S(u"2"), S(u"3")}, ListPadding::Empty},
            ListSource{S(u"list2"), {S(u"a"), S(u"b"), S(u"c")}, ListPadding::Empty},
    };
}

[[nodiscard]] QStringList rows_of(const char16_t* template_text, const ListSourceList& sources) {
    const auto result = evaluate_template(S(template_text), sources);
    REQUIRE(result.ok());
    return result.rows;
}

}  // namespace

// ===========================================================================
// 构想书「使用示例」的 6 个示例 —— 这是 P2 的硬验收标准，逐字对上
// ===========================================================================

TEST_CASE("示例 1：逐行取值 mv $list1[i]$ $list2[i]$") {
    CHECK(rows_of(u"mv $list1[i]$ $list2[i]$", sample_sources()) ==
          QStringList{S(u"mv 1 a"), S(u"mv 2 b"), S(u"mv 3 c")});
}

TEST_CASE("示例 2：笛卡尔积 $matrix(list1,list2,'-')$") {
    const QStringList rows = rows_of(u"$matrix(list1,list2,'-')$", sample_sources());
    REQUIRE(rows.size() == 9);
    CHECK(rows == QStringList{S(u"1-a"),
                              S(u"1-b"),
                              S(u"1-c"),
                              S(u"2-a"),
                              S(u"2-b"),
                              S(u"2-c"),
                              S(u"3-a"),
                              S(u"3-b"),
                              S(u"3-c")});
}

TEST_CASE("示例 3：行号与计数 $i$/$count(list1)$") {
    CHECK(rows_of(u"$i$/$count(list1)$", sample_sources()) ==
          QStringList{S(u"1/3"), S(u"2/3"), S(u"3/3")});
}

TEST_CASE("示例 4：取列表中的项 文件 $index(list2,i)$") {
    CHECK(rows_of(u"文件 $index(list2,i)$", sample_sources()) ==
          QStringList{S(u"文件 a"), S(u"文件 b"), S(u"文件 c")});
}

TEST_CASE("示例 5：嵌套调用 组合 $i$：$matrix(list1,list2,'-')[i]$") {
    CHECK(rows_of(u"组合 $i$：$matrix(list1,list2,'-')[i]$", sample_sources()) ==
          QStringList{S(u"组合 1：1-a"), S(u"组合 2：1-b"), S(u"组合 3：1-c")});
}

TEST_CASE("示例 6：转义 $ 第 $i$ 项：\\$100") {
    CHECK(rows_of(u"第 $i$ 项：\\$100", sample_sources()) ==
          QStringList{S(u"第 1 项：$100"), S(u"第 2 项：$100"), S(u"第 3 项：$100")});
}

// ===========================================================================
// 行数、越界与缺省方式
// ===========================================================================

TEST_CASE("rows 变量给出总行数；i 从 1 起") {
    CHECK(rows_of(u"$i$/$rows$", sample_sources()) == QStringList{S(u"1/3"), S(u"2/3"), S(u"3/3")});
}

TEST_CASE("#list 返回原始长度（不含补位）") {
    // 这个模板没用到行上下文 ⇒ 只求值一次、输出一行（见 engine.hpp 的说明）。
    // 想逐行输出要显式带上 i。
    CHECK(rows_of(u"$#list1$/$count(list1)$", sample_sources()) == QStringList{S(u"3/3")});
    CHECK(rows_of(u"$i$:$#list1$/$count(list1)$", sample_sources()) ==
          QStringList{S(u"1:3/3"), S(u"2:3/3"), S(u"3:3/3")});
}

TEST_CASE("越界索引返回空串而不是 nil") {
    CHECK(rows_of(u"$i$:[$list1[99]$]", sample_sources()) ==
          QStringList{S(u"1:[]"), S(u"2:[]"), S(u"3:[]")});
    CHECK(rows_of(u"[$list1[99]$]", sample_sources()) == QStringList{S(u"[]")});
}

TEST_CASE("Ignore 缺省把行数压到最短列表的长度") {
    ListSourceList sources = sample_sources();
    sources[0].padding = ListPadding::Ignore;  // list1 长度 3 → 截断到 2
    sources[1].items = {S(u"a"), S(u"b")};

    const auto result = evaluate_template(S(u"$list1[i]$-$list2[i]$"), sources);
    REQUIRE(result.ok());
    CHECK(result.rows == QStringList{S(u"1-a"), S(u"2-b")});
}

TEST_CASE("Repeat 缺省让越界读从头重复") {
    ListSourceList sources = ListSourceList{
            ListSource{S(u"list1"), {S(u"1"), S(u"2")}, ListPadding::Repeat},
            ListSource{S(u"list2"), {S(u"a"), S(u"b"), S(u"c")}, ListPadding::Empty},
    };
    const auto result = evaluate_template(S(u"$list1[i]$-$list2[i]$"), sources);
    REQUIRE(result.ok());
    CHECK(result.rows ==
          QStringList{S(u"1-a"), S(u"2-b"), S(u"1-c")});  // 第 3 行回到 list1 的第 1 项
}

TEST_CASE("没有列表时行数为 1，纯字面模板只出一行") {
    CHECK(rows_of(u"固定文本", {}) == QStringList{S(u"固定文本")});
}

TEST_CASE("列表全为空时没有可产出的行") {
    const ListSourceList sources{ListSource{S(u"list1"), {}, ListPadding::Empty}};
    const auto result = evaluate_template(S(u"$list1[i]$"), sources);
    REQUIRE(result.ok());
    CHECK(result.rows.isEmpty());
}

TEST_CASE("空模板输出 0 行且不算错") {
    const auto result = evaluate_template(QString(), sample_sources());
    CHECK(result.ok());
    CHECK(result.rows.isEmpty());
}

// ===========================================================================
// 行展开（技术方案 §3.2）：区段求值为列表时展开为多行
// ===========================================================================

TEST_CASE("裸列表区段逐项展开") {
    CHECK(rows_of(u"$list1$", sample_sources()) == QStringList{S(u"1"), S(u"2"), S(u"3")});
}

TEST_CASE("多个列表区段取笛卡尔积，左者为外层") {
    const QStringList rows = rows_of(u"$list1$-$list2$", sample_sources());
    REQUIRE(rows.size() == 9);
    CHECK(rows.first() == S(u"1-a"));
    CHECK(rows.at(1) == S(u"1-b"));
    CHECK(rows.at(3) == S(u"2-a"));
    CHECK(rows.last() == S(u"3-c"));
}

TEST_CASE("模板不用行上下文时只求值一次（示例 2 依赖这条）") {
    // 若误按 rows 重复求值，$matrix(...)$ 会得到 27 行而不是 9 行
    CHECK(rows_of(u"$matrix(list1,list2,'-')$", sample_sources()).size() == 9);
}

// ===========================================================================
// 错误路径
// ===========================================================================

TEST_CASE("未知列表名报错并列出可用的名字") {
    const auto result = evaluate_template(S(u"$list9[i]$"), sample_sources());
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"list9")));
}

TEST_CASE("区段里的语法错误被定位到具体区段") {
    const auto result = evaluate_template(S(u"a $1 +$ b"), sample_sources());
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"区段")));
}

TEST_CASE("区段只允许表达式：赋值语句编译不过") {
    // 简单模式下不允许语句/赋值（技术方案 §3.4）
    const auto result = evaluate_template(S(u"$x = 1$"), sample_sources());
    CHECK_FALSE(result.ok());
}

TEST_CASE("沙箱中止会透出为求值失败") {
    const auto result =
            evaluate_template(S(u"$(function() while true do end end)()$"), sample_sources());
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"已中止")));
}

// ===========================================================================
// helper 抽检（全量语义见 docs/技术方案与实现路线.md §3.5）
// ===========================================================================

TEST_CASE("列表类 helper") {
    const ListSourceList one{ListSource{S(u"list1"), {S(u"10"), S(u"2"), S(u"1")}, {}}};
    CHECK(rows_of(u"$tolist(sort(list1))[1]$", one).first() == S(u"1"));
    // 自然排序：{10,2,1} → {1,2,10}，于是 2 排在 10 之前
    CHECK(rows_of(u"$natsort(list1)[1]$", one).first() == S(u"1"));
    CHECK(rows_of(u"$natsort(list1)[2]$", one).first() == S(u"2"));
    CHECK(rows_of(u"$concat(slice(list1,1,2),'+')$", one).first() == S(u"10+2"));

    const ListSourceList dup{ListSource{S(u"list1"), {S(u"a"), S(u"a"), S(u"b")}, {}}};
    CHECK(rows_of(u"$count(uniq(list1))$", dup).first() == S(u"2"));
}

TEST_CASE("组合类 helper：zip 按行配对、短的补空串") {
    const ListSourceList sources{
            ListSource{S(u"list1"), {S(u"1"), S(u"2"), S(u"3")}, {}},
            ListSource{S(u"list2"), {S(u"a"), S(u"b")}, {}},
    };
    CHECK(rows_of(u"$zip(list1,list2,'-')[3]$", sources).first() == S(u"3-"));
}

TEST_CASE("生成类 helper：seq / rand 可复现") {
    CHECK(rows_of(u"$concat(seq(3),',')$", {}).first() == S(u"1,2,3"));
    CHECK(rows_of(u"$concat(seq(5,1,-2),',')$", {}).first() == S(u"5,3,1"));

    // seed 固定后结果必须可复现 —— dry-run 与实际执行不能给出不同的随机值
    const auto first = evaluate_template(S(u"$seed(42)$$rand(1000)$"), {});
    const auto second = evaluate_template(S(u"$seed(42)$$rand(1000)$"), {});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.rows == second.rows);
}

TEST_CASE("文本类 helper") {
    CHECK(rows_of(u"$upper('abc')$-$trim('  x  ')$", {}).first() == S(u"ABC-x"));
    CHECK(rows_of(u"$pad('7',3,'0')$", {}).first() == S(u"700"));
    CHECK(rows_of(u"$match('abc123','[0-9]+')$", {}).first() == S(u"123"));
    CHECK(rows_of(u"$fmt('%s=%d','n',7)$", {}).first() == S(u"n=7"));
    CHECK(rows_of(u"$regex('a1b2','[0-9]')[1]$", {}).first() == S(u"1"));  // 无捕获组：整个匹配
    CHECK(rows_of(u"$regex('a1b2','([0-9])')[1]$", {}).first() == S(u"1"));  // 有捕获组：取组
    CHECK(rows_of(u"$count(regex('a1b2','([0-9])'))$", {}).first() == S(u"1"));
}

TEST_CASE("文本类 helper：regex 的第三个参数直接取第 n 项") {
    CHECK(rows_of(u"$regex('第07话', [[第(\\d+)话]], 1)$", {}).first() == S(u"07"));
    // 多个捕获组时 n 就是组号
    CHECK(rows_of(u"$regex('2026-09-15', [[(\\d+)-(\\d+)]] , 2)$", {}).first() == S(u"09"));
    // 越界与不匹配都给空串（与列表越界的约定一致），不会像取下标那样得到 nil
    CHECK(rows_of(u"$regex('第07话', [[第(\\d+)话]], 9)$", {}).first() == S(u""));
    CHECK(rows_of(u"$regex('没有数字', [[(\\d+)]] , 1)$", {}).first() == S(u""));
    // 不给 n 时仍是列表
    CHECK(rows_of(u"$count(regex('2026-09-15', [[(\\d+)-(\\d+)]]))$", {}).first() == S(u"2"));
}

TEST_CASE("文本类 helper：replace 是字面替换，resub 才是正则") {
    // 字面：`.` 就是点本身，`[0-9]` 就是这五个字符
    CHECK(rows_of(u"$replace('2026.09.15','.','-')$", {}).first() == S(u"2026-09-15"));
    CHECK(rows_of(u"$replace('a1b2','[0-9]','#')$", {}).first() == S(u"a1b2"));
    CHECK(rows_of(u"$replace('a-b-c','-','')$", {}).first() == S(u"abc"));  // 全部出现都换

    // 正则：全局替换 + 捕获组引用
    CHECK(rows_of(u"$resub('a1b2','[0-9]','#')$", {}).first() == S(u"a#b#"));
    CHECK(rows_of(u"$resub('第07话', [[第(\\d+)话]], [[第\\1话 正片]])$", {}).first() ==
          S(u"第07话 正片"));

    // 正则串写坏了当场报错，而不是静默按字面处理
    CHECK_FALSE(evaluate_template(S(u"$resub('x','(','y')$"), {}).ok());
    // 空串在字面替换里是"每个字符之间都插一段"，明令禁止
    CHECK_FALSE(evaluate_template(S(u"$replace('x','','y')$"), {}).ok());
}

TEST_CASE("路径类 helper 同时接受 / 与 \\") {
    CHECK(rows_of(u"$basename('D:\\\\a\\\\b\\\\c.txt')$", {}).first() == S(u"c.txt"));
    CHECK(rows_of(u"$dirname('D:\\\\a\\\\b\\\\c.txt')$", {}).first() == S(u"D:/a/b"));
    CHECK(rows_of(u"$ext('a/b/c.txt')$", {}).first() == S(u"txt"));
    CHECK(rows_of(u"$stem('a/b/c.txt')$", {}).first() == S(u"a/b/c"));
    CHECK(rows_of(u"$join('a','b','c.txt')$", {}).first() == S(u"a/b/c.txt"));
}

TEST_CASE("类型类 helper") {
    CHECK(rows_of(u"$num('42')+1$", {}).first() == S(u"43"));
    CHECK(rows_of(u"$str(42)$", {}).first() == S(u"42"));
}

TEST_CASE("未白名单化的全局名读不到") {
    // 读未知全局名走 env 的 __index ⇒ nil ⇒ 调用时报错；不能是真实存在的库
    for (const char16_t* name : {u"$io.open$",
                                 u"$os.time$",
                                 u"$require('x')$",
                                 u"$print(1)$",
                                 u"$dofile('x')$",
                                 u"$load('return 1')$",
                                 u"$debug.getinfo(1)$",
                                 u"$collectgarbage('count')$",
                                 u"$coroutine.create(function() end)$",
                                 u"$setmetatable({}, {})$"}) {
        const auto result = evaluate_template(S(name), {});
        CHECK_FALSE(result.ok());
    }
}
