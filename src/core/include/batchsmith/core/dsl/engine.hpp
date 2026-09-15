#pragma once

#include <QString>
#include <QStringList>

#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/sandbox/sandbox.hpp"

namespace batchsmith::core::dsl {

struct BatchResult {
    QStringList rows;
    QString error;  ///< 非空即失败（编译错、运行时错、或沙箱中止）

    /// 这一批的**输入行数**（`i` 的取值范围是 1…row_count，`rows` 模板变量的值）。
    ///
    /// 为什么单独给出来：`rows.size()` 是**输出**行数，两者在行展开下并不相等
    /// （`$matrix(...)$` 会让一行变成多行）。需要"输出第 k 行对应哪个输入行"的调用方
    /// （`bs plan` 要把行绑到文件上）必须能看见这个数，否则只能自己再算一遍行数规则 ——
    /// 那就等于把「有 Ignore 取最短、否则取最长」这条规则复制成两份。
    ///
    /// 失败退出时保持 0。
    qsizetype row_count = 0;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }

    /// 输出行与输入行**一一对应**吗？（`bs plan` 的 rename 计划要求这一点）
    [[nodiscard]] bool rows_are_one_to_one() const {
        return ok() && row_count > 0 && rows.size() == row_count;
    }
};

/// 求值：**编译一次 → 逐行调用**（技术方案 §3.1），在沙箱中执行（ADR-6）。
///
/// ## 行数与「用不用行上下文」
///
/// - 行数 = 各输入列表长度：有 `Ignore` 缺省的列表参与时取**最小值**，否则取**最大值**
///   （构想书「默认按行逐一匹配，行数取最长列表的长度」+「Ignore 参与时行数取最小值」）。
///   没有任何输入列表时行数为 1。
/// - `i` 从 1 起，`rows` 为总行数，二者由沙箱按行注入。
/// - **模板若在第 1 行没有用到 `i` / `rows`，就只求值一次。** 这条不是优化，而是语义：
///   否则 `$matrix(list1,list2,'-')$` 这种「区段本身就是多行输出」的模板会被重复
///   `rows` 遍（3 个列表项 × 9 个组合 = 27 行），与构想书示例给出的 9 行冲突。
///   用到行上下文时才逐行求值 —— `$list1[i]$` 这类写法因此天然是「逐行对应」。
///
/// ## 行展开（技术方案 §3.2）
///
/// 一次求值中若某区段返回列表：长度 n > 1 则该行展开为 n 行；多个列表区段取**笛卡尔积**，
/// 左者为外层、**右者为内层**；长度为 1 视作标量；长度为 0 则该行被跳过；
/// 字面段随展开广播。
[[nodiscard]] BatchResult evaluate_template(const QString& template_text,
                                            const ListSourceList& sources,
                                            const sandbox::Limits& limits = {});

}  // namespace batchsmith::core::dsl
