#include "batchsmith/core/dsl/template_scanner.hpp"

namespace batchsmith::core::dsl {

namespace {

constexpr QChar kDelimiter = u'$';
constexpr QChar kEscape = u'\\';

}  // namespace

ScanResult scan_template(const QString& template_text) {
    ScanResult result;

    QString buffer;
    bool in_section = false;
    bool escaped = false;

    // 出错时**清空已扫描的段**再返回：半个结果没有任何用处，留着只会让调用方
    // 误用。契约写在头文件里：只看 error 是否为空。
    const auto fail = [&result](const QString& message) -> ScanResult {
        result.segments.clear();
        result.error = message;
        return result;
    };

    // 处理完一段就把它按「当前所处的种类」落进结果。
    // ⚠️ in_section 必须按引用捕获：按值捕获会冻结成定义时的快照（false），
    // 于是所有区段都会被误判成字面段 —— 这个 bug 单测能抓到，但不该留到那时候。
    const auto flush = [&result, &buffer, &in_section] {
        if (!buffer.isEmpty()) {
            result.segments.append(
                    {in_section ? Segment::Kind::Section : Segment::Kind::Literal, buffer});
        }
        buffer.clear();
    };

    for (const QChar ch : template_text) {
        if (escaped) {
            // 只有 `\$` 是转义；其余 `\x` 原样保留（连同反斜杠），
            // 否则 `C:\temp` 会被吃掉一个字符。
            if (ch != kDelimiter) {
                buffer.append(kEscape);
            }
            buffer.append(ch);
            escaped = false;
            continue;
        }

        if (ch == kEscape) {
            escaped = true;
            continue;
        }

        if (ch == kDelimiter) {
            if (in_section && buffer.isEmpty()) {
                return fail(QStringLiteral("区段为空：`$` 之间必须有内容"));
            }
            flush();
            in_section = !in_section;
            continue;
        }

        buffer.append(ch);
    }

    if (escaped) {
        buffer.append(kEscape);  // 结尾的孤立反斜杠按字面量处理
    }
    if (in_section) {
        return fail(QStringLiteral("有未配对的 `$`：区段没有闭合"));
    }
    flush();

    return result;
}

}  // namespace batchsmith::core::dsl
