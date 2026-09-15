#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/preset/preset.hpp"

/// ## 计划（Plan）是什么
///
/// 一次批量操作的**完整描述**，在真正动文件之前先算出来给人看：每一行"哪个文件 → 变成
/// 什么名字"、有没有冲突。技术方案 §4.1 的 `plan → apply` 两阶段里，这是第一阶段，
/// **默认停在这里**（dry-run）。
///
/// 三条契约：
///
/// 1. **纯数据 + 纯函数**：`build_plan()` 只读预设与文件系统（扫描、`stat`），
///    **不写任何东西**；算完的结果全在 `Plan` 里，渲染、将来的 apply 与撤销日志
///    都只读它。所以"预览与实际执行一致"是可以被检查的性质，而不是靠自觉。
/// 2. **不确定的东西不留到执行时**：能在这里判定的问题（源不在、目标重名、目标已存在、
///    目标逃出绑定根）就在这里判完并写进行的状态 —— 执行阶段不该再冒出新的意外。
/// 3. **不猜**：拿不准的（比如有两个文件夹来源、模板一行产出多个名字）**报错**，
///    不挑一个"看起来合理"的继续做。批量操作里"猜错了"的代价是文件被改成别的名字。
///
/// ## 与 Phase 4 的分工
///
/// 本模块只做到"算出来 + 打印"。**执行（apply）、两阶段重命名（批内互换、仅大小写不同）、
/// 撤销日志、apply 前的 mtime/size 复校**都属于 Phase 4 —— 那些是"真的动文件"的部分。
namespace batchsmith::core::plan {

/// 计划里一行的状态。
///
/// `Ready` 与 `Unchanged` 之外的都是**问题行**：它们不会被应用到（Phase 4 执行时直接跳过
/// 并计入失败），所以在计划阶段看见它们、改掉它们，是这一步存在的意义。
enum class RowState {
    Ready,      ///< 可以执行：源在、目标是新名字、没有冲突
    Unchanged,  ///< 目标与源同名：本来就对，无需动作（**不算问题**）
    NoSource,  ///< 这一行没有对应的文件（列表不是文件夹来源，或该行落在列表长度之外）
    SourceMissing,    ///< 源文件不在了
    TargetEmpty,      ///< 求值结果是空串 —— 没有名字可改
    TargetNotInside,  ///< 目标不是"绑定根里面的一个名字"（绝对路径、`..`、驱动器相对路径…）
    TargetExists,     ///< 目标已存在，而且不是本次批次里的源
    DuplicateTarget,  ///< 与另一行的目标同名（会互相覆盖）
    DuplicateSource,  ///< 同一个源被两行用到（一个文件要给两个名字）
};

/// 计划里的一行。
struct PlanRow {
    /// **1 起**的行号，与模板里的 `i` 一致 —— 用户核对时说的"第 7 行"就是这个数。
    int index = 0;

    /// 源：**相对绑定根**的路径（`/` 分隔），与列表里的条目同形，便于肉眼核对。
    /// 没有文件对应时为空。
    QString source;

    /// 目标：模板的求值结果。相对绑定根 —— rename 模式下它是同一个文件夹里的新名字。
    QString target;

    /// 源的绝对路径。没有文件维度时为空。
    QString source_path;

    /// 目标的绝对路径。没有文件维度、或目标不是合法名字时为空。
    QString target_path;

    /// 这一行为什么进了这个状态（一行短说明，打印时用）。
    /// 状态枚举只有粗分类，具体原因（"目标以路径分隔符结尾"）写在这里 ——
    /// 用户看到的必须是"哪里不对"，而不是"这类不对"。
    QString note;

    RowState state = RowState::NoSource;
};

/// 一份计划。
struct Plan {
    QString preset_name;
    QString preset_path;  ///< 预设文件路径（便于把计划与来源对上）

    /// 输出方式。当前只可能是 `rename`。
    QString output_mode;

    /// 充当**文件维度**的列表 id；空 = 这个预设没有绑定文件夹。
    QString file_list;

    /// 文件维度的根目录（绝对路径）；空 = 没有文件维度。
    QString root;

    QList<PlanRow> rows;

    /// 计划级提醒：不是错误，但用户必须看见
    /// （例如"这批有 3 行属于批内互换，执行时要走两阶段重命名"）。
    QStringList notes;

    [[nodiscard]] int count(RowState state) const;

    /// 问题行数（`Ready` / `Unchanged` 之外的行）。
    [[nodiscard]] int problem_count() const;

    [[nodiscard]] bool has_problems() const { return problem_count() > 0; }
};

struct PlanBuild {
    Plan plan;
    QString error;  ///< 非空即"计划没生成出来"

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// 算出计划。**只读**：不创建、不修改、不删除任何文件或目录。
///
/// ## 文件维度是哪一列
///
/// rename 要回答"第 7 行是哪个文件"，所以必须有一个列表充当**文件维度**：
/// 它的条目就是被重命名的那些文件。约定（写死在这里，免得以后被无声改掉）：
///
/// - **唯一的**文件夹来源（`kind = dir`）列表充当文件维度；
/// - 一个都没有：所有行标 `NoSource`，计划照样打印目标名（用来看表达式对不对）；
/// - **有两个及以上：报错**，把候选列出来。挑第一个是"猜"，而猜错的后果是把文件
///   改成别的名字 —— 这种歧义必须由用户消掉（把一个列改成手输，或拆成两个预设）。
///
/// ## 一行必须恰好一个名字
///
/// `rename` 要求模板**逐行**产出名字（`rows.size() == row_count`）。
/// 行展开（`$matrix(...)$` 一行变多行）与"没用行上下文所以整批只求值一次"都会让输出行数
/// 与输入行数不等，那种模板需要的是 stdout 模式（Phase 5），不是重命名 ——
/// 此时报错并说清是哪种情况，而不是硬凑出一份计划。
[[nodiscard]] PlanBuild build_plan(const Preset& preset,
                                   const SlotBindings& bindings,
                                   const sandbox::Limits& limits = {});

/// 行状态的中文短标签（打印给人看）。
[[nodiscard]] QString row_state_label(RowState state);

/// 行状态的 ASCII 词（给 JSON / 脚本用）。**改动它等于改接口**，要同步 CHANGELOG。
[[nodiscard]] QString row_state_token(RowState state);

/// 渲染成给人看的计划（就是"打印 diff"）。
[[nodiscard]] QString plan_to_text(const Plan& plan);

/// 序列化成 JSON。给脚本消费，也留给 Phase 4 的"执行同一份计划"用。
[[nodiscard]] QString plan_to_json(const Plan& plan);

}  // namespace batchsmith::core::plan
