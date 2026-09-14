#pragma once

#include <QString>
#include <QStringList>

#include "batchsmith/core/list/list_source.hpp"

namespace batchsmith::core::dsl {

struct EvaluationResult {
    QStringList rows;
    QString error;  ///< 非空即求值失败

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// 展开行数的硬上限。
///
/// 多个列表的笛卡尔积增长极快（技术方案 §8 风险登记表里那条「行数爆炸」）。
/// 界面上做这件事必须设闸：不设的话 `$list1$-$list2$` 两个万行列表就能把
/// 主线程连同内存一起拖死。超过上限不截断、直接报错 —— 静默截断会让人以为
/// 算对了。
inline constexpr qsizetype kMaxExpandedRows = 100000;

/// 简单模式求值器：把模板算成「输出行」。
///
/// ⚠️ **这是临时实现，Phase 2 由 Lua 编译器取代。** 它只覆盖「区段里只写列表名」
/// 这一子集（`$list1$`），目的是让界面先跑通「输入表达式 → 得到输出列表」这条
/// 交互链路。任何 helper、算术、索引、`i`/`rows` 都属于 Phase 2 的 DSL 编译器 +
/// 沙箱（技术方案 §3、ADR-5 单运行时），这里**明确报错而不是猜一个语义出来**。
///
/// 展开规则严格按技术方案 §3.2，不做任何改动：
///
/// - 区段求值为长度 n > 1 的列表 → 该行展开为 n 行；
/// - 多个区段同时是列表 → **笛卡尔积**，顺序按区段从左到右（**右者为内层循环**）；
/// - 区段求值为长度 1 的列表 → 相当于标量，不展开；
/// - 区段求值为长度 0 的列表 → 该行被跳过（结果为空）；
/// - 字面段随展开**广播**到每一行。
///
/// 与完整模型的唯一差别：这里固定 **宿主行数 rows = 1**。完整模型里 `i`/`rows`
/// 由输入列表长度决定、经 Lua 运行时注入，因此本求值器的行为是完整模型的
/// **严格子集**，两个例子：
///
/// ```text
/// $list1$                 -> list1 的全部项，逐行                            （3 项 → 3 行）
/// mv $list1$ out          -> "mv a out" / "mv b out" / ...                   （字面段广播）
/// $list1$-$list2$         -> 9 行，list2 为内层循环                          （笛卡尔积）
/// ```
[[nodiscard]] EvaluationResult evaluate_simple(const QString& template_text,
                                               const ListSourceList& sources);

}  // namespace batchsmith::core::dsl
