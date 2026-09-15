#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace batchsmith::core::dsl {

/// 一个工具函数的文档条目。
struct HelperDoc {
    QString group;  ///< 分组名（列表 / 组合 / 生成 / 文本 / 路径 / 文件名 / 类型）
    QStringList names;  ///< 本条目覆盖的函数名（成对出现的如 upper/lower 是两个）
    QString signature;  ///< 展示用签名，形如 `slice(list, from[, to])`
    QString summary;    ///< 一句话说明

    // names 是刻意与 signature 分开的：测试拿 names 与沙箱**实际注册**的名字集合做
    // 双向比对（既防笔误、也防新增函数忘了写文档）。若从 signature 里正则提取，
    // 参数名（list、from、to）会被误当成函数名，那比对就不可信了。
};

/// 一条**可执行**示例。
///
/// 这是这份速查表能被信任的原因：示例是数据，不是散文。
/// `tests/core/test_cheatsheet.cpp` 会把每一条真的求值一遍，
/// 并核对输出是否等于 `expect` —— 于是**帮助文档与实现不可能漂移**：
/// 实现改了而文档没改，CI 直接红。
struct CheatExample {
    QString group;       ///< 分组名
    QString title;       ///< 这条例子的说明
    QString expression;  ///< DSL 表达式
    QStringList list1;   ///< 注入的 list1（可为空）
    QStringList list2;   ///< 注入的 list2（可为空）
    QStringList expect;  ///< 期望输出；为空表示「只断言求值成功」
};

/// 速查表的一个章节（供界面的目录导航使用）。
struct CheatSection {
    QString anchor;  ///< HTML 锚点，如 `sec-helpers`
    QString title;   ///< 章节标题
};

/// 工具函数文档，顺序与 `helper::registered_names()` 一致（列表/组合/生成/文本/路径/类型）。
[[nodiscard]] QList<HelperDoc> helper_docs();

/// 可执行示例（测试会逐条求值核对）。
[[nodiscard]] QList<CheatExample> cheat_examples();

/// 顶层章节列表（顺序即文档顺序）。界面用它生成目录，测试用它核对渲染结果。
[[nodiscard]] QList<CheatSection> cheatsheet_sections();

/// 渲染成**纯文本**速查表。CLI 的 `bs cheatsheet` 用它 —— 终端里 HTML 没有意义。
[[nodiscard]] QString cheatsheet_text();

/// 渲染成**自包含 HTML**（内联样式、无外部资源）—— 界面的帮助窗口用它，
/// 于是章节标题、可点击的目录、表格都能有。
///
/// 与 `cheatsheet_text()` 由**同一份数据**渲染，不存在"两个版本各说各话"的可能；
/// 测试会分别核对两份输出都含全部函数名与章节标题。
[[nodiscard]] QString cheatsheet_html();

/// 渲染成 CHM 的**目录文件**（`.hhc`，HTML Help 的 site map 格式）。
///
/// 这是 Windows 版帮助手册（.chm）左侧那棵树。它同样由 `cheatsheet_sections()` /
/// `helper_docs()` / `cheat_examples()` 生成 —— 所以 CHM 的目录、界面帮助窗口的目录、
/// `bs cheatsheet` 的章节，三者永远一致，没有手工维护的中间产物。
///
/// 两级结构：顶层是九个章节（个数与标题都在 `cheatsheet_sections()` 里，别在这里写死），
/// 二级是"工具函数"与"示例"里的分组。
/// 二级节点跳转到 HTML 里对应的分组标题（锚点由同一份数据生成，见 `cheatsheet_html()`）。
[[nodiscard]] QString cheatsheet_hhc();

}  // namespace batchsmith::core::dsl
