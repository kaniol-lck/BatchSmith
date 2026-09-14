#include <doctest/doctest.h>

#include <QString>

#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/sandbox/sandbox.hpp"

using batchsmith::core::ListPadding;
using batchsmith::core::ListSource;
using batchsmith::core::ListSourceList;
using batchsmith::core::dsl::evaluate_template;
using batchsmith::core::sandbox::Limits;

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 时间与指令数给足，专门观察某一条限制
[[nodiscard]] Limits limits_with(qsizetype max_instructions,
                                 qsizetype max_memory_bytes,
                                 qsizetype max_string_bytes,
                                 qsizetype max_wall_ms) {
    Limits limits;
    limits.max_instructions = max_instructions;
    limits.max_memory_bytes = max_memory_bytes;
    limits.max_string_bytes = max_string_bytes;
    limits.max_wall_ms = max_wall_ms;
    return limits;
}

}  // namespace

// ===========================================================================
// 四重限制（ADR-6）
// ===========================================================================

TEST_CASE("死循环被指令数上限中止") {
    // 区段里虽然不能写语句，但匿名函数体里可以 —— 恶意预设就是这么干的
    const auto result =
            evaluate_template(S(u"$(function() local i = 0 while true do i = i + 1 end end)()$"),
                              {},
                              limits_with(500'000, 64LL * 1024 * 1024, 1LL * 1024 * 1024, 60'000));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"已中止")));
    CHECK(result.error.contains(S(u"指令数")));
}

TEST_CASE("墙钟超时同样能拦住（指令数上限放宽时）") {
    const auto result = evaluate_template(
            S(u"$(function() local i = 0 while true do i = i + 1 end end)()$"),
            {},
            limits_with(10'000'000'000LL, 64LL * 1024 * 1024, 1LL * 1024 * 1024, 200));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"已中止")));
    CHECK(result.error.contains(S(u"超时")));
}

TEST_CASE("内存上限：构造大表被拦下") {
    // 上限取 8MB 而不是 2MB：沙箱自身（Lua 状态 + env + 28 个 helper 闭包 + 四张库）
    // 在有的编译器/运行库下基线就接近 2MB，那样**构造期**就会超限，报错信息里也就没有
    // 「已中止」—— 用例会以"看不出原因"的方式失败（MSVC 上真踩过）。
    // 下面的循环本身要分配几十 MB，所以 8MB 仍然稳稳触发。
    const auto result = evaluate_template(
            S(u"$(function() local t = {} for i = 1, 1000000 do t[i] = 'xxxxxxxxxxxxxxxx' end "
              "return #t end)()$"),
            {},
            limits_with(10'000'000'000LL, 8LL * 1024 * 1024, 1LL * 1024 * 1024, 60'000));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"已中止")));
    CHECK(result.error.contains(S(u"内存")));
}

TEST_CASE("逃逸面 1：string.rep 被参数预检拦下（先算规模再分配）") {
    const auto result = evaluate_template(S(u"$string.rep('x', 1000000000)$"), {});
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"超过单次字符串上限")));
}

TEST_CASE("逃逸面 1：方法调用写法也绕不过去（string 表已被就地覆写）") {
    const auto result = evaluate_template(S(u"$('x'):rep(1000000000)$"), {});
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"超过单次字符串上限")));
}

TEST_CASE("逃逸面 1：string.format 的超大宽度被拦下") {
    const auto result = evaluate_template(S(u"$string.format('%99999999d', 1)$"), {});
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"宽度过大")));
}

TEST_CASE("逃逸面 1：table.concat 先把总长算出来") {
    // 分隔符也算进去，否则 10 字节的分隔符乘十万项这种就能溜过去
    const auto result =
            evaluate_template(S(u"$table.concat(tolist(seq(100000)), 'xxxxxxxxxx')$"), {});
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"超过单次字符串上限")));
}

TEST_CASE("逃逸面 2：coroutine 未暴露") {
    const ListSourceList sources{};
    const auto result = evaluate_template(S(u"$coroutine.create(function() end)$"), sources);
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"nil")));
}

TEST_CASE("逃逸面 3：pcall 吞掉中止错误，宿主照样终止整批") {
    // 指令数限制被触发后，脚本用 pcall 捕获并"继续"，宿主侧标记必须仍然生效
    const auto result = evaluate_template(
            S(u"$(function() pcall(function() while true do end end) return '假装没事' end)()$"),
            {},
            limits_with(500'000, 64LL * 1024 * 1024, 1LL * 1024 * 1024, 60'000));
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"已中止")));
}

TEST_CASE("collectgarbage 未暴露（否则能压掉内存上限的效果）") {
    for (const char16_t* expression :
         {u"$collectgarbage('count')$", u"$setmetatable({}, {})$", u"$getmetatable(list1)$"}) {
        const ListSourceList sources{ListSource{S(u"list1"), {S(u"a")}, ListPadding::Empty}};
        const auto result = evaluate_template(S(expression), sources);
        CHECK_FALSE(result.ok());
    }
}

// ===========================================================================
// 内置库的可用性（白名单子集）
// ===========================================================================

TEST_CASE("基础库白名单可用：type / tostring / pairs / pcall") {
    const ListSourceList sources{ListSource{S(u"list1"), {S(u"a")}, ListPadding::Empty}};
    CHECK(evaluate_template(S(u"$type(list1)$"), sources).rows.first() == S(u"table"));
    CHECK(evaluate_template(S(u"$type(i)$"), sources).rows.first() == S(u"number"));
    CHECK(evaluate_template(S(u"$tostring(1 + 1)$"), {}).rows.first() == S(u"2"));
    CHECK(evaluate_template(S(u"$pcall(function() return 1 end)$"), {}).rows.first().isEmpty()
                  ? false
                  : true);
    CHECK(evaluate_template(S(u"$(function() local n = 0 for _ in pairs({a=1,b=2}) do n = n + 1 "
                              "end return n end)()$"),
                            {})
                  .rows.first() == S(u"2"));
}

TEST_CASE("print 不暴露（批处理工具没有输出通道）") {
    const auto result = evaluate_template(S(u"$print('hi')$"), {});
    CHECK_FALSE(result.ok());
}

TEST_CASE("math.random 换成沙箱的固定随机源，seed 后可复现") {
    const auto first = evaluate_template(S(u"$seed(7)$$math.random(100)$"), {});
    const auto second = evaluate_template(S(u"$seed(7)$$math.random(100)$"), {});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.rows == second.rows);

    // 不 seed 时默认种子固定 ⇒ 同一次会话里两次求值结果一致
    const auto a = evaluate_template(S(u"$math.random(100)$"), {});
    const auto b = evaluate_template(S(u"$math.random(100)$"), {});
    CHECK(a.rows == b.rows);
}

TEST_CASE("行数超过上限时报错而不是静默截断") {
    Limits limits = limits_with(10'000'000'000LL, 256LL * 1024 * 1024, 8LL * 1024 * 1024, 60'000);
    limits.max_rows = 10;

    QStringList wide;
    for (int i = 0; i < 50; ++i) {
        wide.append(QString::number(i));
    }
    const ListSourceList sources{ListSource{S(u"list1"), wide, ListPadding::Empty}};

    const auto result = evaluate_template(S(u"$i$-$list1[i]$"), sources, limits);
    CHECK_FALSE(result.ok());
    CHECK(result.error.contains(S(u"上限")));
}
