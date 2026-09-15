#include "batchsmith/core/plan/plan.hpp"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringView>

namespace batchsmith::core::plan {

namespace {

// ---------------------------------------------------------------------------
// 终端对齐：中日韩字符占两列
//
// 只为了把"源 → 目标"两张表对齐，好让用户一列扫下来就能核对行与文件的对应关系
// （这条路线的安全模型正依赖这个动作，见技术方案 §4.4）。宽度表不可能完全准确
// （终端与字体各有各的想法），所以这是**近似**：宁可某几行差一列，也不要因为
// 追求精确而把这段逻辑做成一个需要维护字符表的子系统。
// ---------------------------------------------------------------------------

bool is_wide_char(char32_t code) {
    return (code >= 0x1100 && code <= 0x115F) ||  // 谚文字母
           (code >= 0x2E80 && code <= 0xA4CF) ||  // 中日韩部首、假名、注音、汉字
           (code >= 0xAC00 && code <= 0xD7A3) ||  // 谚文音节
           (code >= 0xF900 && code <= 0xFAFF) ||  // 兼容汉字
           (code >= 0xFE30 && code <= 0xFE6F) ||  // 中日韩兼容形式
           (code >= 0xFF00 && code <= 0xFF60) ||  // 全角形式
           (code >= 0xFFE0 && code <= 0xFFE6) ||  // 全角符号
           (code >= 0x20000 && code <= 0x3FFFD);  // 扩展区汉字
}

int display_width(QStringView text) {
    int width = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char32_t code = text.at(i).unicode();
        width += is_wide_char(code) ? 2 : 1;
    }
    return width;
}

/// 右补空格到 `width` 列（已经够宽就原样返回，绝不截断 —— 截断会藏掉信息）。
QString pad_right(QStringView text, int width) {
    const int current = display_width(text);
    if (current >= width) {
        return text.toString();
    }
    return text.toString() + QString(current > 0 ? width - current : 0, QLatin1Char(' '));
}

/// 目标必须是"绑定文件夹里面的一个名字"。合法时返回空串，否则返回拒绝的原因。
///
/// 为什么这些一律**拒绝**而不是"尽力解释"：目标跑出绑定根意味着批量操作会写到
/// 用户根本没看的地方（`../` 往上一层、绝对路径跳到别的盘）。而计划里显示的路径
/// 是按绑定根算的，用户核对的和他实际会得到的不是同一个东西 —— 这种"看到的与实际
/// 不一致"必须当成错误，而不是警告。
QString path_form_problem(QStringView target) {
    const QString text = target.toString();

    if (QDir::isAbsolutePath(text)) {
        return QStringLiteral("目标是绝对路径，会写到绑定文件夹之外");
    }

#if defined(Q_OS_WIN)
    // `C:名字` 是"驱动器相对路径"：QDir::isAbsolutePath() 不认它，但它落到哪个盘
    // 取决于进程的当前目录 —— 一次重命名可能悄悄写到别的盘上。
    static const QRegularExpression drive_relative(QStringLiteral("^[A-Za-z]:(?![/\\\\])"));
    if (drive_relative.match(text).hasMatch()) {
        return QStringLiteral("目标是驱动器相对路径（C:名字），落点取决于当前目录");
    }
#endif

    if (text.endsWith(QLatin1Char('/')) || text.endsWith(QLatin1Char('\\'))) {
        return QStringLiteral("目标以路径分隔符结尾，那是个文件夹名");
    }

    const QString cleaned = QDir::cleanPath(text);
    if (cleaned == QLatin1String(".")) {
        return QStringLiteral("目标就是文件夹本身");
    }
    if (cleaned == QLatin1String("..") || cleaned.startsWith(QLatin1String("../"))) {
        return QStringLiteral("目标跑到了绑定文件夹之外（..）");
    }
    return {};
}

/// 行号的打印宽度：4 列够到 9999 行，再多也只是不齐、不会丢信息。
constexpr int kIndexWidth = 4;

/// 对齐用的列宽上限。列表里的名字再长也不该把整张表推得没法看；
/// 超过就不补空格（行会不齐，但每个名字都是完整的）。
constexpr int kMaxPadWidth = 40;

}  // namespace

// ---------------------------------------------------------------------------
// Plan 的统计
// ---------------------------------------------------------------------------

int Plan::count(RowState state) const {
    int total = 0;
    for (const PlanRow& row : rows) {
        if (row.state == state) {
            ++total;
        }
    }
    return total;
}

int Plan::problem_count() const {
    int total = 0;
    for (const PlanRow& row : rows) {
        if (row.state != RowState::Ready && row.state != RowState::Unchanged) {
            ++total;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// 生成计划
// ---------------------------------------------------------------------------

PlanBuild build_plan(const Preset& preset,
                     const SlotBindings& bindings,
                     const sandbox::Limits& limits) {
    PlanBuild out;
    out.plan.preset_name = preset.name;
    out.plan.preset_path = preset.file_path;
    out.plan.output_mode = preset.output_mode;

    // 防御性断言：output_mode 的合法性在 preset_from_toml() 里已经校验过一遍，
    // 这里再挡一次 —— 手工构造出来的 Preset 也不该让问题留到"动文件"那一步才出现。
    if (preset.output_mode != QLatin1String("rename")) {
        out.error = QStringLiteral("输出方式是 %1，本版本只会算 rename 计划"
                                   "（argv / stdout 属于后续阶段）")
                            .arg(preset.output_mode);
        return out;
    }

    // 取数：槽位替换 + 扫描（只读）。**有一列读不到就报错** —— 计划必须建立在真实数据上；
    // 拿一条空列接着算，用户看到的是"这批什么都不做"，而不是"路径绑错了"。
    QHash<QString, QString> scan_errors;
    const ListSourceList sources = preset_sources(preset, bindings, &scan_errors);
    if (!scan_errors.isEmpty()) {
        QStringList details;
        // 按预设里的列表顺序报，别按 QHash 的遍历顺序 —— 报错也要稳定可预期
        for (const PresetList& item : preset.lists) {
            const auto found = scan_errors.constFind(item.id);
            if (found != scan_errors.constEnd()) {
                details.append(QStringLiteral("  %1：%2").arg(item.id, found.value()));
            }
        }
        out.error = QStringLiteral("列表读不到数，计划算不出来：\n%1").arg(details.join('\n'));
        return out;
    }

    // 文件维度：唯一的文件夹来源列表（约定写死在 plan.hpp 的说明里）
    QList<const ListSource*> dir_lists;
    for (const ListSource& source : sources) {
        if (source.kind == ListSourceKind::Directory) {
            dir_lists.append(&source);
        }
    }
    if (dir_lists.size() > 1) {
        QStringList names;
        names.reserve(dir_lists.size());
        for (const ListSource* source : dir_lists) {
            names.append(source->name);
        }
        out.error = QStringLiteral("预设里有 %1 个文件夹来源的列表（%2）：rename 必须先知道"
                                   "哪一列是被改名的那些文件，本版本只支持一个 —— "
                                   "把其余列改成手输，或拆成两个预设。")
                            .arg(dir_lists.size())
                            .arg(names.join(QStringLiteral("、")));
        return out;
    }
    const ListSource* file_list = dir_lists.isEmpty() ? nullptr : dir_lists.first();
    if (file_list != nullptr) {
        out.plan.file_list = file_list->name;
        // 根取规范化路径：这样"目标有没有跑出绑定根"是按真实位置比较的，
        // 而不是按用户写的那串（可能是软链接、可能带 ./）
        out.plan.root = QFileInfo(file_list->dir.path).canonicalFilePath();
        if (out.plan.root.isEmpty()) {
            out.plan.root = QDir(file_list->dir.path).absolutePath();
        }
    }

    const dsl::BatchResult evaluated =
            dsl::evaluate_template(preset.template_text, sources, limits);
    if (!evaluated.ok()) {
        out.error = evaluated.error;
        return out;
    }

    // ---- 没有文件维度：照样算，但每行都标明"没有对应的文件" ----
    if (file_list == nullptr) {
        out.plan.notes.append(QStringLiteral(
                "预设里没有文件夹来源的列表：这些行没有对应的文件，计划只能显示表达式算出来的"
                "名字。rename 需要一列文件 —— 把文件夹拖到那一列上，或用 --bind 绑定槽位。"));
        int index = 0;
        for (const QString& target : evaluated.rows) {
            ++index;
            PlanRow row;
            row.index = index;
            row.target = target;
            row.state = RowState::NoSource;
            row.note = QStringLiteral("没有文件维度");
            out.plan.rows.append(row);
        }
        return out;
    }

    // ---- 有文件维度：行数与名字必须一一对应 ----
    if (evaluated.row_count <= 0) {
        // 列表为空不是错误，但必须说出来：不然"什么都没做"看起来像正常结果
        out.plan.notes.append(QStringLiteral("列表 %1 里一个条目都没有（检查绑定路径与 filter）—— "
                                             "这批什么都不会做。")
                                      .arg(file_list->name));
        return out;
    }
    if (evaluated.rows.size() != evaluated.row_count) {
        out.error =
                QStringLiteral("模板没有逐行产出名字（输入 %1 行，输出 %2 行）：rename 要求"
                               "一行一个名字。\n常见原因：模板里没用 i / rows（整批只求值一次），"
                               "或某个区段返回了列表导致行展开（$matrix(…)$ 那种）。"
                               "这两种都是 stdout 模式的用法（Phase 5），不是重命名。")
                        .arg(evaluated.row_count)
                        .arg(evaluated.rows.size());
        return out;
    }

    out.plan.rows.reserve(evaluated.rows.size());
    for (qsizetype k = 0; k < evaluated.rows.size(); ++k) {
        PlanRow row;
        row.index = static_cast<int>(k) + 1;
        row.source = file_list->value_at(k);
        row.target = evaluated.rows.at(k);
        row.state = RowState::Ready;  // 占位：下面按顺序逐条判定，后面的判定可以推翻它
        out.plan.rows.append(row);
    }

    // ---- 第一遍：只看这一行自己 ----
    for (PlanRow& row : out.plan.rows) {
        if (row.source.isEmpty()) {
            row.state = RowState::NoSource;
            row.note = QStringLiteral("列表 %1 的条目不够长，这一行没有对应的文件")
                               .arg(file_list->name);
            continue;
        }
        row.source_path = QDir(out.plan.root).filePath(row.source);

        // 源不在：扫描刚做完就发现文件没了，说明扫描与这里之间目录被动过。
        // 必须当场标出来 —— 拿一个不存在的源去改名，用户只会看到"这一行失败"。
        if (!QFileInfo::exists(row.source_path)) {
            row.state = RowState::SourceMissing;
            row.note = QStringLiteral("文件不在（扫描之后被挪走或删掉了？）");
            continue;
        }

        if (row.target.isEmpty()) {
            row.state = RowState::TargetEmpty;
            row.note = QStringLiteral("求值结果是空串，没有名字可改");
            continue;
        }
        const QString problem = path_form_problem(row.target);
        if (!problem.isEmpty()) {
            row.state = RowState::TargetNotInside;
            row.note = problem;
            continue;
        }
        row.target_path = QDir(out.plan.root).filePath(row.target);
    }

    // ---- 第二遍：与别的行有关的问题（重名、同一个源、目标已被占） ----
    //
    // 只在"暂时还算可执行"的行之间比。已经有问题的行不会被执行，把它们算进冲突里
    // 会给另一行扣一顶莫名其妙的帽子（"与第 12 行同名"，而第 12 行自己就是错的）。
    //
    // 比较**一律折叠大小写**：Windows 与 macOS 的文件系统默认不敏感，而"同一份规格
    // 在三个平台给出同一个答案"是本项目的一贯要求（见 glob_match 与 natural_compare
    // 的同款决定）。折叠的后果是偶尔多报一次冲突 —— 比在 Windows 上静默互相覆盖好。
    QHash<QString, QList<int>> rows_of_target;  // 折叠后的目标 → 用到它的行号（升序）
    QHash<QString, QList<int>> rows_of_source;
    for (const PlanRow& row : out.plan.rows) {
        if (row.state != RowState::Ready) {
            continue;
        }
        rows_of_target[row.target.toCaseFolded()].append(row.index);
        rows_of_source[row.source.toCaseFolded()].append(row.index);
    }

    /// 在同一组的行号里挑一个**不是自己**的，用来写"与第 N 行…"。
    /// 指到自己（"第 1 行与第 1 行同名"）会让用户怀疑报告本身。
    const auto other_row = [](const QList<int>& indexes, int self) {
        for (const int index : indexes) {
            if (index != self) {
                return index;
            }
        }
        return self;
    };

    // "这批会腾出来的名字"：可执行行的源都会被改成别的名字，于是那个名字空出来了。
    // 批内互换（a→b 且 b→a）靠的就是它 —— 那不是冲突，是 Phase 4 要用两阶段
    // 重命名处理的情形。
    QSet<QString> freed_names;
    for (const PlanRow& row : out.plan.rows) {
        if (row.state == RowState::Ready) {
            freed_names.insert(row.source.toCaseFolded());
        }
    }

    int swapped = 0;
    int case_only = 0;
    for (PlanRow& row : out.plan.rows) {
        if (row.state != RowState::Ready) {
            continue;
        }
        const QString source_key = row.source.toCaseFolded();
        const QString target_key = row.target.toCaseFolded();

        const QList<int> same_source = rows_of_source.value(source_key);
        if (same_source.size() > 1) {
            row.state = RowState::DuplicateSource;
            row.note = QStringLiteral("同一个文件被 %1 行用到（第 %2 行也用它）")
                               .arg(same_source.size())
                               .arg(other_row(same_source, row.index));
            continue;
        }
        const QList<int> same_target = rows_of_target.value(target_key);
        if (same_target.size() > 1) {
            row.state = RowState::DuplicateTarget;
            row.note = QStringLiteral("与第 %1 行同名（会互相覆盖）")
                               .arg(other_row(same_target, row.index));
            continue;
        }
        // 先判"本来就对"，再判"目标已存在"：目标等于源时那个名字当然存在，
        // 但那是这个文件自己占着的，不是冲突。
        if (row.target == row.source) {
            row.state = RowState::Unchanged;
            row.note = QStringLiteral("名字本来就是对的");
            continue;
        }
        if (QFileInfo::exists(row.target_path) && !freed_names.contains(target_key)) {
            row.state = RowState::TargetExists;
            row.note = QStringLiteral("目标已存在，而且不是这批里的文件");
            continue;
        }
        if (source_key == target_key) {
            // 只改大小写：在大小写不敏感的文件系统上，"把 a.mkv 改成 A.mkv" 会撞上
            // 自己，执行时必须借一个临时名 —— 属于 Phase 4 的两阶段重命名。
            ++case_only;
            row.note = QStringLiteral("只改大小写：Windows / macOS 上执行时要借临时名");
            continue;
        }
        if (freed_names.contains(target_key)) {
            ++swapped;
        }
    }

    if (swapped > 0) {
        out.plan.notes.append(
                QStringLiteral("有 %1 行的目标是这批里另一个文件的当前名字（互换/接龙）："
                               "执行时要走两阶段重命名（Phase 4），现在只是列出来。")
                        .arg(swapped));
    }
    if (case_only > 0) {
        out.plan.notes.append(QStringLiteral("有 %1 行只改大小写：Windows / macOS 上执行时"
                                             "同样要借临时名（Phase 4）。")
                                      .arg(case_only));
    }
    return out;
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

QString row_state_label(RowState state) {
    switch (state) {
        case RowState::Ready:
            return QStringLiteral("可执行");
        case RowState::Unchanged:
            return QStringLiteral("无需动作");
        case RowState::NoSource:
            return QStringLiteral("没有对应的文件");
        case RowState::SourceMissing:
            return QStringLiteral("源文件不在");
        case RowState::TargetEmpty:
            return QStringLiteral("目标是空");
        case RowState::TargetNotInside:
            return QStringLiteral("目标跑到绑定文件夹之外");
        case RowState::TargetExists:
            return QStringLiteral("目标已存在");
        case RowState::DuplicateTarget:
            return QStringLiteral("与另一行同名");
        case RowState::DuplicateSource:
            return QStringLiteral("同一个文件被多行用到");
    }
    return QStringLiteral("未知状态");
}

QString row_state_token(RowState state) {
    switch (state) {
        case RowState::Ready:
            return QStringLiteral("ready");
        case RowState::Unchanged:
            return QStringLiteral("unchanged");
        case RowState::NoSource:
            return QStringLiteral("no_source");
        case RowState::SourceMissing:
            return QStringLiteral("source_missing");
        case RowState::TargetEmpty:
            return QStringLiteral("target_empty");
        case RowState::TargetNotInside:
            return QStringLiteral("target_not_inside");
        case RowState::TargetExists:
            return QStringLiteral("target_exists");
        case RowState::DuplicateTarget:
            return QStringLiteral("duplicate_target");
        case RowState::DuplicateSource:
            return QStringLiteral("duplicate_source");
    }
    return QStringLiteral("unknown");
}

QString plan_to_text(const Plan& plan) {
    QStringList lines;

    lines.append(QStringLiteral("计划 · %1")
                         .arg(plan.preset_name.isEmpty() ? QStringLiteral("（未命名预设）")
                                                         : plan.preset_name));
    lines.append(QStringLiteral("预设        %1")
                         .arg(plan.preset_path.isEmpty() ? QStringLiteral("（还没保存过）")
                                                         : plan.preset_path));
    lines.append(QStringLiteral("输出方式    %1（内置重命名，不经 shell）").arg(plan.output_mode));
    if (plan.root.isEmpty()) {
        lines.append(QStringLiteral("文件维度    （没有绑定文件夹）"));
    } else {
        lines.append(QStringLiteral("文件维度    %1  ←  %2").arg(plan.file_list, plan.root));
    }
    lines.append(QStringLiteral("共 %1 行：可执行 %2 · 无需动作 %3 · 有问题 %4")
                         .arg(plan.rows.size())
                         .arg(plan.count(RowState::Ready))
                         .arg(plan.count(RowState::Unchanged))
                         .arg(plan.problem_count()));

    if (plan.rows.isEmpty()) {
        lines.append(QStringLiteral("（没有行）"));
    } else {
        lines.append(QString(48, QChar(0x2500)));  // ── 分隔线

        int source_width = 0;
        int target_width = 0;
        for (const PlanRow& row : plan.rows) {
            source_width = std::max(source_width, display_width(row.source));
            target_width = std::max(target_width, display_width(row.target));
        }
        source_width = std::min(source_width, kMaxPadWidth);
        target_width = std::min(target_width, kMaxPadWidth);

        for (const PlanRow& row : plan.rows) {
            const bool ok = row.state == RowState::Ready || row.state == RowState::Unchanged;
            const QString marker = ok ? QStringLiteral("→") : QStringLiteral("!");
            const QString source_field = row.source.isEmpty() ? QStringLiteral("-") : row.source;
            const QString target_field =
                    row.target.isEmpty() ? QStringLiteral("（空）") : row.target;

            // 有问题才补到列宽并接上说明；没问题的不补 —— 免得每行尾巴上留一串空格
            QString line;
            if (row.note.isEmpty()) {
                line = QStringLiteral("%1  %2  %3  %4")
                               .arg(QString::number(row.index).rightJustified(kIndexWidth),
                                    pad_right(source_field, source_width),
                                    marker,
                                    target_field);
            } else {
                line = QStringLiteral("%1  %2  %3  %4  %5")
                               .arg(QString::number(row.index).rightJustified(kIndexWidth),
                                    pad_right(source_field, source_width),
                                    marker,
                                    pad_right(target_field, target_width),
                                    row.note);
            }
            lines.append(line);
        }
    }

    if (!plan.notes.isEmpty()) {
        lines.append(QString());
        for (const QString& note : plan.notes) {
            lines.append(QStringLiteral("注意  %1").arg(note));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString plan_to_json(const Plan& plan) {
    QJsonObject root;
    root.insert(QStringLiteral("preset"), plan.preset_name);
    root.insert(QStringLiteral("preset_path"), plan.preset_path);
    root.insert(QStringLiteral("output"), plan.output_mode);
    root.insert(QStringLiteral("file_list"), plan.file_list);
    root.insert(QStringLiteral("root"), plan.root);

    QJsonArray rows;
    for (const PlanRow& row : plan.rows) {
        QJsonObject item;
        item.insert(QStringLiteral("i"), row.index);
        item.insert(QStringLiteral("source"), row.source);
        item.insert(QStringLiteral("target"), row.target);
        item.insert(QStringLiteral("state"), row_state_token(row.state));
        if (!row.source_path.isEmpty()) {
            item.insert(QStringLiteral("source_path"), row.source_path);
        }
        if (!row.target_path.isEmpty()) {
            item.insert(QStringLiteral("target_path"), row.target_path);
        }
        if (!row.note.isEmpty()) {
            item.insert(QStringLiteral("note"), row.note);
        }
        rows.append(item);
    }
    root.insert(QStringLiteral("rows"), rows);

    QJsonObject counts;
    counts.insert(QStringLiteral("rows"), static_cast<int>(plan.rows.size()));
    counts.insert(QStringLiteral("ready"), plan.count(RowState::Ready));
    counts.insert(QStringLiteral("unchanged"), plan.count(RowState::Unchanged));
    counts.insert(QStringLiteral("problems"), plan.problem_count());
    root.insert(QStringLiteral("counts"), counts);

    root.insert(QStringLiteral("notes"), QJsonArray::fromStringList(plan.notes));

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace batchsmith::core::plan
