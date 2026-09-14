#include <doctest/doctest.h>

#include <QString>

#include "batchsmith/core/dsl/compiler.hpp"

using batchsmith::core::dsl::compile_template;
using batchsmith::core::dsl::escape_lua_string;

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

}  // namespace

TEST_CASE("字面转义：引号、反斜杠与换行都变成转义序列，产物保持单行") {
    CHECK(escape_lua_string(S(u"普通")) == S(u"\"普通\""));
    CHECK(escape_lua_string(S(u"带\"引号")) == S(u"\"带\\\"引号\""));
    CHECK(escape_lua_string(S(u"反\\斜杠")) == S(u"\"反\\\\斜杠\""));

    // 换行必须写成 \n，否则「一行 DSL ↔ 一行 Lua」的行号映射会被破坏（§3.3）
    const QString multi = escape_lua_string(S(u"第一行\n第二行"));
    CHECK_FALSE(multi.contains(QLatin1Char('\n')));
    CHECK(multi == S(u"\"第一行\\n第二行\""));

    CHECK(escape_lua_string(S(u"制表\t符")) == S(u"\"制表\\t符\""));
}

TEST_CASE("编译产物：区段包在括号里、段值原样返回") {
    const auto compiled = compile_template(S(u"mv $list1$ out"));
    REQUIRE(compiled.ok());
    CHECK(compiled.section_count == 1);
    REQUIRE(compiled.segments.size() == 3);  // 字面 / 区段 / 字面

    // 括号是必需的：它把多变返回值截断成一个，否则返回值个数会与段数错位
    CHECK(compiled.lua_source.contains(S(u"local s1 = (list1)")));
    CHECK(compiled.lua_source.contains(S(u"return \"mv \", s1, \" out\"")));
}

TEST_CASE("多个区段按顺序返回，与段序列逐位对应") {
    const auto compiled = compile_template(S(u"$i$/$count(list1)$"));
    REQUIRE(compiled.ok());
    CHECK(compiled.section_count == 2);
    CHECK(compiled.lua_source.contains(S(u"local s1 = (i)")));
    CHECK(compiled.lua_source.contains(S(u"local s2 = (count(list1))")));
    CHECK(compiled.lua_source.contains(S(u"return s1, \"/\", s2")));
}

TEST_CASE("每个区段单独占一行，行号 = 区段序号 + 1（用于错误定位）") {
    const auto compiled = compile_template(S(u"a$b$c$d$"));
    REQUIRE(compiled.ok());

    const QStringList lines = compiled.lua_source.split(QLatin1Char('\n'));
    REQUIRE(lines.size() >= 4);
    CHECK(lines.at(0).contains(S(u"return function")));
    CHECK(lines.at(1).contains(S(u"s1")));  // 第 1 个区段
    CHECK(lines.at(2).contains(S(u"s2")));  // 第 2 个区段
}

TEST_CASE("纯字面模板也能编译，返回单个字面段") {
    const auto compiled = compile_template(S(u"没有区段"));
    REQUIRE(compiled.ok());
    CHECK(compiled.section_count == 0);
    CHECK(compiled.segments.size() == 1);
    CHECK(compiled.lua_source.contains(S(u"return \"没有区段\"")));
}

TEST_CASE("扫描阶段的错误原样透出") {
    const auto unpaired = compile_template(S(u"$list1"));
    CHECK_FALSE(unpaired.ok());
    CHECK(unpaired.error.contains(S(u"未配对")));

    const auto empty = compile_template(S(u"$$"));
    CHECK_FALSE(empty.ok());
    CHECK(empty.error.contains(S(u"区段为空")));
}

TEST_CASE("空模板编译成合法代码") {
    const auto compiled = compile_template(QString());
    CHECK(compiled.ok());
    CHECK(compiled.section_count == 0);
    CHECK(compiled.segments.isEmpty());
    CHECK(compiled.lua_source.contains(S(u"return function")));
}
