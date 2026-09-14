#include "batchsmith/core/dsl/simple_evaluator.hpp"

#include <QRegularExpression>

#include "batchsmith/core/dsl/template_scanner.hpp"

namespace batchsmith::core::dsl {

namespace {

const ListSource* find_source(const ListSourceList& sources, const QString& name) {
    for (const ListSource& source : sources) {
        if (source.name == name) {
            return &source;
        }
    }
    return nullptr;
}

/// 名字是不是一个「裸标识符」。用来区分两类错误：
/// 写了个不存在的列表名（多半是打错），还是写了需要 Lua 的表达式。
bool is_bare_identifier(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(text).hasMatch();
}

/// 技术方案 §3.5 里定义的**上下文变量**（不是列表名）。
///
/// 单列出来是因为 `$i$` 若走「未知标识符」那条分支，会得到
/// 「没有名为 i 的列表」这种**误导性**报错 —— 用户明明是在用文档里写过的变量。
/// 对它们应当直说：这是 Phase 2 的 Lua 运行时提供的。
bool is_context_variable(const QString& text) {
    return text == QStringLiteral("i") || text == QStringLiteral("rows");
}

QString join_names(const ListSourceList& sources) {
    QStringList names;
    names.reserve(sources.size());
    for (const ListSource& source : sources) {
        names.append(source.name);
    }
    return names.join(QStringLiteral("、"));
}

}  // namespace

EvaluationResult evaluate_simple(const QString& template_text, const ListSourceList& sources) {
    EvaluationResult result;

    const ScanResult scan = scan_template(template_text);
    if (!scan.ok()) {
        result.error = scan.error;
        return result;
    }
    if (scan.segments.isEmpty()) {
        return result;  // 空模板：合法，输出 0 行
    }

    // 从「一行空前缀」开始，逐个区段把行集合展开。
    QStringList rows{QString()};

    for (const Segment& segment : scan.segments) {
        if (segment.kind == Segment::Kind::Literal) {
            // 字面段广播到当前每一行（技术方案 §3.2）
            for (QString& row : rows) {
                row.append(segment.text);
            }
            continue;
        }

        const QString expression = segment.text.trimmed();
        const ListSource* source = find_source(sources, expression);
        if (source == nullptr) {
            // 报错分两种，别混：打错列表名，还是用了尚未接入的 Lua 语义。
            // `i` / `rows` 属于后者 —— 它们是文档里定义的上下文变量。
            if (is_bare_identifier(expression) && !is_context_variable(expression)) {
                result.error = QStringLiteral("没有名为 `%1` 的列表（现有：%2）")
                                       .arg(expression, join_names(sources));
            } else {
                result.error =
                        QStringLiteral("区段 `$%1$` 需要 Lua 求值，尚未接入：简单模式目前"
                                       "只支持 `$list1$` 这类列表名引用。helper、索引、算术、"
                                       "`i`/`rows` 将在 Phase 2 随 DSL 编译器一起提供。")
                                .arg(segment.text);
            }
            return result;
        }

        // 长度为 0 的列表 → 该行被跳过
        if (source->items.isEmpty()) {
            return result;
        }

        // 长度为 1 → 视作标量，不产生展开
        if (source->items.size() == 1) {
            for (QString& row : rows) {
                row.append(source->items.first());
            }
            continue;
        }

        // 行数闸门：宁可报错，也不让界面被笛卡尔积拖死
        if (rows.size() > kMaxExpandedRows / source->items.size()) {
            result.error = QStringLiteral("展开后行数超过上限（%1 行），已中止。"
                                          "请减少列表长度或区段数量。")
                                   .arg(kMaxExpandedRows);
            return result;
        }

        QStringList expanded;
        expanded.reserve(rows.size() * source->items.size());
        for (const QString& row : rows) {                // 左侧区段 = 外层循环
            for (const QString& item : source->items) {  // 右侧区段 = 内层循环
                expanded.append(row + item);
            }
        }
        rows = std::move(expanded);
    }

    result.rows = rows;
    return result;
}

}  // namespace batchsmith::core::dsl
