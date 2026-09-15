#include <doctest/doctest.h>

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include "batchsmith/core/dsl/cheatsheet.hpp"
#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/helper/helpers.hpp"
#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/version.hpp"

using batchsmith::core::ListPadding;
using batchsmith::core::ListSource;
using batchsmith::core::ListSourceList;
using batchsmith::core::dsl::cheat_examples;
using batchsmith::core::dsl::CheatExample;
using batchsmith::core::dsl::cheatsheet_hhc;
using batchsmith::core::dsl::cheatsheet_html;
using batchsmith::core::dsl::cheatsheet_sections;
using batchsmith::core::dsl::cheatsheet_text;
using batchsmith::core::dsl::evaluate_template;
using batchsmith::core::dsl::helper_docs;
using batchsmith::core::helper::registered_names;

namespace {

/// 把示例自带的列表数据装成列表源（名字固定为 list1 / list2）
[[nodiscard]] ListSourceList sources_of(const CheatExample& example) {
    ListSourceList sources;
    if (!example.list1.isEmpty()) {
        sources.append(ListSource{QStringLiteral("list1"), example.list1, ListPadding::Empty});
    }
    if (!example.list2.isEmpty()) {
        sources.append(ListSource{QStringLiteral("list2"), example.list2, ListPadding::Empty});
    }
    return sources;
}

}  // namespace

// ===========================================================================
// 速查表是「帮助文档」，但它不是散文 —— 它是数据。
//
// 下面这几条测试的意义在于：**帮助文档与实现不可能漂移**。
// 新增了一个 helper 却忘了写进速查表，或者文档里写了个不存在的函数名，
// 或者某条示例的输出跟实现不符 —— CI 会直接红。
//
// 这比"写文档时仔细一点"可靠得多：那种约定靠的是人，而人一定会忘。
// ===========================================================================

TEST_CASE("速查表：文档里的函数名与实际注册的一一对应") {
    const QStringList registered = registered_names();

    QStringList documented;
    for (const auto& doc : helper_docs()) {
        documented.append(doc.names);
    }

    const QSet<QString> registered_set(registered.cbegin(), registered.cend());
    const QSet<QString> documented_set(documented.cbegin(), documented.cend());

    // 遗漏：沙箱注册了，但速查表里没写 —— 用户不会知道有这个函数
    QStringList missing;
    for (const QString& name : registered) {
        if (!documented_set.contains(name)) {
            missing.append(name);
        }
    }
    CHECK_MESSAGE(missing.isEmpty(),
                  "这些工具函数没有写进速查表: ",
                  missing.join(QStringLiteral("、")).toStdString());

    // 笔误：速查表里写了，但沙箱里没有 —— 照着帮助写会直接报错
    QStringList unknown;
    for (const QString& name : documented) {
        if (!registered_set.contains(name)) {
            unknown.append(name);
        }
    }
    CHECK_MESSAGE(unknown.isEmpty(),
                  "速查表里写了不存在的函数: ",
                  unknown.join(QStringLiteral("、")).toStdString());

    // 数量对不上时也要显式失败：这提醒改代码的人「文档也要一起看」
    CHECK(registered.size() == 33);
    CHECK(documented_set.size() == 33);
}

TEST_CASE("速查表：每条函数文档的字段都是完整的") {
    for (const auto& doc : helper_docs()) {
        CAPTURE(doc.signature.toStdString());
        CHECK_FALSE(doc.group.isEmpty());
        CHECK_FALSE(doc.names.isEmpty());
        CHECK_FALSE(doc.signature.isEmpty());
        CHECK_FALSE(doc.summary.isEmpty());
    }
}

TEST_CASE("速查表：每条示例都能求值成功，且输出与声明一致") {
    const auto examples = cheat_examples();
    REQUIRE(examples.size() >= 20);  // 示例太少说明这份文档没跟上功能

    for (const auto& example : examples) {
        CAPTURE(example.title.toStdString());
        CAPTURE(example.expression.toStdString());

        CHECK_FALSE(example.expression.isEmpty());
        CHECK_FALSE(example.group.isEmpty());
        CHECK_FALSE(example.title.isEmpty());

        const auto result = evaluate_template(example.expression, sources_of(example));
        REQUIRE_MESSAGE(result.ok(), "示例求值失败: ", result.error.toStdString());

        if (example.expect.isEmpty()) {
            // 声明了"只断言成功"（例如随机数：固定种子下可复现，但值本身不该写死）
            CHECK(result.rows.size() >= 1);
        } else {
            CHECK(result.rows == example.expect);
        }
    }
}

TEST_CASE("速查表：渲染出来的文本包含全部函数名与版本号") {
    const QString text = cheatsheet_text();
    REQUIRE_FALSE(text.isEmpty());

    // 每个函数名都要在正文里出现（带左括号，避免 "str" 命中 "string" 这类误判）
    for (const QString& name : registered_names()) {
        CHECK_MESSAGE(text.contains(name + QLatin1Char('(')),
                      "速查表正文里找不到函数: ",
                      name.toStdString());
    }

    CHECK(text.contains(QString::fromLatin1(batchsmith::core::version_string())));

    // 三条最容易踩的坑必须写明 —— 它们是实测出来的，不写用户一定会撞
    CHECK(text.contains(QStringLiteral("逐行对应")));
    CHECK(text.contains(QStringLiteral("笛卡尔积")));
    CHECK(text.contains(QStringLiteral("只求值一次")));
}

TEST_CASE("速查表：HTML 版与纯文本版说的是同一件事") {
    const QString html = cheatsheet_html();
    const QString text = cheatsheet_text();
    REQUIRE(html.startsWith(QStringLiteral("<!DOCTYPE html>")));

    // 章节标题：两版都要有；HTML 还要有对应的目录锚点与章节 id，否则目录点了不跳
    for (const auto& section : cheatsheet_sections()) {
        CAPTURE(section.title.toStdString());
        CHECK(text.contains(section.title));
        CHECK(html.contains(section.title.toHtmlEscaped()));
        CHECK(html.contains(QStringLiteral("href=\"#") + section.anchor + QStringLiteral("\"")));
        CHECK(html.contains(QStringLiteral("id=\"") + section.anchor + QStringLiteral("\"")));
    }

    // 函数名与示例表达式：两版都要有 —— 只改了其中一版会在这里红
    for (const auto& doc : helper_docs()) {
        for (const QString& name : doc.names) {
            CAPTURE(name.toStdString());
            CHECK(text.contains(name + QLatin1Char('(')));
            CHECK(html.contains(name + QLatin1Char('(')));
        }
    }
    for (const auto& example : cheat_examples()) {
        CAPTURE(example.expression.toStdString());
        CHECK(text.contains(example.expression));
        CHECK(html.contains(example.expression.toHtmlEscaped()));
    }
}

TEST_CASE("速查表：CHM 目录（.hhc）与 HTML 的锚点一一对应") {
    const QString hhc = cheatsheet_hhc();
    const QString html = cheatsheet_html();
    REQUIRE(hhc.contains(QStringLiteral("<!DOCTYPE HTML PUBLIC")));

    // 每个章节都要有目录项，且跳转目标在 HTML 里真实存在
    for (const auto& section : cheatsheet_sections()) {
        CAPTURE(section.title.toStdString());
        CHECK(hhc.contains(section.title));
        CHECK(hhc.contains(QStringLiteral("index.html#") + section.anchor));
        CHECK(html.contains(QStringLiteral("id=\"") + section.anchor + QStringLiteral("\"")));
    }

    // 二级节点：分组锚点必须与 HTML 里的一致 ——
    // 不一致的后果是"CHM 左侧树点进去跳不到那一组"，而这种问题很难被测出来，
    // 所以这里反过来从 HTML 里把所有分组锚点抓出来，逐个核对 hhc 有没有跳转目标。
    static const QRegularExpression group_id(QStringLiteral("h3 id=\"((?:grp|ex)-[0-9]+)\""));
    auto matches = group_id.globalMatch(html);
    qsizetype group_count = 0;
    while (matches.hasNext()) {
        const QString id = matches.next().captured(1);
        CAPTURE(id.toStdString());
        ++group_count;
        CHECK(hhc.contains(QStringLiteral("index.html#") + id));
    }
    CHECK(group_count == 13);  // 7 组工具函数 + 6 组示例

    // 目录项总数 = 章节数 + 分组数。写成推导而不是硬编码：
    // 加一节时不必改这里，而这个式子仍然能抓住"漏了目录项"。
    CHECK(hhc.count(QStringLiteral("<param name=\"Name\"")) ==
          cheatsheet_sections().size() + group_count);

    // 每一节都必须有正文 —— 漏写正文时渲染器会留下一句占位话，
    // 那种帮助看起来"有标题、没内容"，比报错更难发现
    CHECK_FALSE(html.contains(QStringLiteral("（这一节还没有正文）")));
}
