#pragma once

#include <QList>
#include <QString>

namespace batchsmith::core::dsl {

/// 模板里的一段。
///
/// 扫描阶段只区分「字面段」与「区段」两类（技术方案 §3.1）。
/// 区段内的表达式**不做解析**——那是编译阶段的事；这里只保证切分正确。
struct Segment {
    enum class Kind {
        Literal,  ///< 原样输出的文本（`\$` 已被还原为 `$`）
        Section,  ///< `$...$` 里的表达式原文（去掉首尾空格前的原文）
    };

    Kind kind = Kind::Literal;
    QString text;
};

struct ScanResult {
    QList<Segment> segments;
    QString error;  ///< 非空即编译失败

    /// ⚠️ 契约：**失败时 `segments` 保证为空**。半截扫描结果没有任何用处，
    /// 留着只会让调用方误用；判断成功与否只看 error 是否为空。
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// 把模板扫描成「字面段 | 区段」序列。
///
/// 规则（技术方案 §3.4，**不自行发明**）：
///
/// - 区段由 `$` 界定，**区段内首个未转义的 `$` 即结束区段**。规则单一、可教学。
/// - `\$` 转义为字面 `$`，在字面段与区段内都有效。
/// - 模板中**未配对落单的 `$` 是编译错误**（fail loud），不静默当字面量。
/// - 空区段（`$$`）同样是错误 —— 它一定是笔误，不该被猜成空串。
///
/// 除 `\$` 之外不为 `\` 定义任何转义：`C:\path` 这类字面文本必须原样保留，
/// 不能让反斜杠被吞掉。
[[nodiscard]] ScanResult scan_template(const QString& template_text);

}  // namespace batchsmith::core::dsl
