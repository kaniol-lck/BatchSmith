#include "batchsmith/core/dsl/compiler.hpp"

#include "batchsmith/core/dsl/template_scanner.hpp"

namespace batchsmith::core::dsl {

QString escape_lua_string(const QString& text) {
    QString out;
    out.reserve(text.size() + 2);
    out.append(QLatin1Char('"'));

    for (const QChar ch : text) {
        switch (ch.unicode()) {
            case u'\\':
                out.append(QLatin1String("\\\\"));
                break;
            case u'"':
                out.append(QLatin1String("\\\""));
                break;
            case u'\n':
                out.append(QLatin1String("\\n"));
                break;
            case u'\r':
                out.append(QLatin1String("\\r"));
                break;
            case u'\t':
                out.append(QLatin1String("\\t"));
                break;
            default:
                if (ch.unicode() < 0x20 || ch.unicode() == 0x7F) {
                    // 其余不可打印字符用三位十进制转义，产物依然是单行
                    out.append(QStringLiteral("\\%1").arg(ch.unicode(), 3, 10, QLatin1Char('0')));
                } else if (ch.unicode() > 0x7F) {
                    // 非 ASCII 直接以 UTF-16 写出：Lua 源码按 UTF-8 交给 luaL_loadbuffer，
                    // UTF-8 字节序是合法的 Lua 字符串内容
                    out.append(ch);
                } else {
                    out.append(ch);
                }
                break;
        }
    }

    out.append(QLatin1Char('"'));
    return out;
}

CompiledTemplate compile_template(const QString& template_text) {
    CompiledTemplate result;

    const ScanResult scan = scan_template(template_text);
    if (!scan.ok()) {
        result.error = scan.error;
        return result;
    }

    QString body;
    QString returned;
    result.segments = scan.segments;  // 与返回值逐位对应，宿主据此解释结果

    for (const Segment& segment : scan.segments) {
        if (segment.kind == Segment::Kind::Section) {
            ++result.section_count;
            const QString local = QStringLiteral("s%1").arg(result.section_count);

            // 一行一个区段：行号即区段序号，Lua 报错时好对号入座。
            // 括号是必需的，见头文件说明。
            body += QStringLiteral("  local %1 = (%2)\n").arg(local, segment.text);

            if (!returned.isEmpty()) {
                returned += QStringLiteral(", ");
            }
            returned += local;
        } else {
            if (!returned.isEmpty()) {
                returned += QStringLiteral(", ");
            }
            returned += escape_lua_string(segment.text);
        }
    }

    if (result.section_count == 0 && returned.isEmpty()) {
        // 空模板：给一个合法但空的结果，由调用方决定「0 行」
        returned = QStringLiteral("\"\"");
    }

    result.lua_source = QStringLiteral("return function()\n%1  return %2\nend\n")
                                .arg(body.isEmpty() ? QString() : body, returned);
    return result;
}

}  // namespace batchsmith::core::dsl
