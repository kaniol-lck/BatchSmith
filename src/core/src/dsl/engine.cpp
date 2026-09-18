#include "batchsmith/core/dsl/engine.hpp"

#include <algorithm>
#include <limits>

#include <QRegularExpression>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "batchsmith/core/dsl/compiler.hpp"
#include "batchsmith/core/dsl/template_scanner.hpp"

namespace batchsmith::core::dsl {

namespace {

/// 行数：有 Ignore 参与取最小值，否则取最大值（构想书）。
/// 没有任何输入列表时给 1；列表全为空时给 0（空列表自然后续会被跳过）。
qsizetype total_rows_for(const ListSourceList& sources) {
    if (sources.isEmpty()) {
        return 1;
    }
    bool has_ignore = false;
    qsizetype longest = 0;
    qsizetype shortest = std::numeric_limits<qsizetype>::max();
    for (const ListSource& source : sources) {
        has_ignore = has_ignore || source.padding == ListPadding::Ignore;
        longest = std::max(longest, source.size());
        shortest = std::min(shortest, source.size());
    }
    return has_ignore ? shortest : longest;
}

/// 从 Lua 报错信息里抠出行号，并换算回区段序号（编译器保证「行号 = 区段序号 + 1」）
QString describe_error(const CompiledTemplate& compiled, const QString& raw) {
    static const QRegularExpression line_pattern(QStringLiteral(":(\\d+):"));
    const QRegularExpressionMatch match = line_pattern.match(raw);

    if (match.hasMatch()) {
        const int line = match.captured(1).toInt();
        const int section = line - 1;  // 第 1 行是 `return function(i)`
        if (section >= 1 && section <= compiled.section_count) {
            int seen = 0;
            for (const Segment& segment : compiled.segments) {
                if (segment.kind != Segment::Kind::Section) {
                    continue;
                }
                if (++seen == section) {
                    return QStringLiteral("区段 `$%1$` 出错：%2")
                            .arg(segment.text, raw.mid(match.capturedEnd(0)).trimmed());
                }
            }
        }
    }
    return raw;
}

/// 一次求值中，某一段的结果
struct Part {
    bool is_list = false;
    QString text;
    QStringList items;
};

QString value_to_qstring(lua_State* state, int index, bool* ok) {
    *ok = true;
    switch (lua_type(state, index)) {
        case LUA_TNIL:
            return QString();
        case LUA_TBOOLEAN:
            return lua_toboolean(state, index) != 0 ? QStringLiteral("true")
                                                    : QStringLiteral("false");
        case LUA_TNUMBER:
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            return QString::fromUtf8(text, static_cast<qsizetype>(length));
        }
        default:
            *ok = false;
            return {};
    }
}

}  // namespace

BatchResult evaluate_template(const QString& template_text,
                              const ListSourceList& sources,
                              const sandbox::Limits& limits) {
    BatchResult result;

    const CompiledTemplate compiled = compile_template(template_text);
    if (!compiled.ok()) {
        result.error = compiled.error;
        return result;
    }
    if (compiled.segments.isEmpty()) {
        return result;  // 空模板：合法，0 行
    }

    const qsizetype total_rows = total_rows_for(sources);
    if (total_rows <= 0) {
        return result;  // 输入列表为空：没有可产出的行
    }
    if (total_rows > limits.max_rows) {
        result.error = QStringLiteral("行数超过上限（%1）").arg(limits.max_rows);
        return result;
    }

    sandbox::Sandbox box(limits);
    if (box.state() == nullptr) {
        result.error = box.violation_message();
        return result;
    }
    box.set_phase("eval.inject_lists");
    box.inject_lists(sources);
    box.trace_violation_message("注入列表之后（还没跑任何 Lua）");

    lua_State* state = box.state();
    box.set_phase("eval.load");
    const QByteArray source = compiled.lua_source.toUtf8();
    if (luaL_loadbuffer(state, source.constData(), static_cast<size_t>(source.size()), "=dsl") !=
        LUA_OK) {
        result.error = describe_error(compiled,
                                      QString::fromUtf8(lua_tostring(state, -1) == nullptr
                                                                ? "编译失败"
                                                                : lua_tostring(state, -1)));
        return result;
    }

    // 把 chunk 的 `_ENV` 换成沙箱的白名单 env，然后执行得到 function(i)
    lua_getfield(state, LUA_REGISTRYINDEX, "batchsmith.env");
    box.set_phase("eval.chunk");
    lua_setupvalue(state, -2, 1);
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        result.error = describe_error(compiled,
                                      QString::fromUtf8(lua_tostring(state, -1) == nullptr
                                                                ? "求值失败"
                                                                : lua_tostring(state, -1)));
        return result;
    }
    const int function_ref = luaL_ref(state, LUA_REGISTRYINDEX);

    const int segment_count = static_cast<int>(compiled.segments.size());
    QStringList rows;

    for (qsizetype row = 1; row <= total_rows; ++row) {
        lua_settop(state, 0);
        box.clear_row_context_used();
        box.set_row_context(row, total_rows);

        box.set_phase("eval.row");
        lua_rawgeti(state, LUA_REGISTRYINDEX, function_ref);
        // 不传参：i / rows 由 env 注入（见 compiler.hpp 的第 0 条）
        if (lua_pcall(state, 0, segment_count, 0) != LUA_OK) {
            // 诊断探针：把「raise 返回 → 中止分支」这一段再切一刀。
            // 此刻 `lua_pcall` 刚返回、**还没**构造下面的 `raw`，所以这一次读数能区分
            // 「释放发生在 Lua 的错误传播里」与「发生在引擎这几行里」。
            box.trace_violation_message("lua_pcall 返回之后（还没构造 raw）");
            const QString raw = QString::fromUtf8(
                    lua_tostring(state, -1) == nullptr ? "求值失败" : lua_tostring(state, -1));
            if (box.violation() != sandbox::Violation::None) {
                // 限制被触发：宿主侧标记优先 —— 脚本里 pcall 吞掉错误也照样中止整批
                // 诊断探针：与 raise() 里那一次对起来看，就知道消息的数据块是在哪一步丢的。
                box.trace_violation_message("中止分支：拷贝之前");
                result.error = QStringLiteral("已中止：%1").arg(box.violation_message());
            } else {
                result.error = describe_error(compiled, raw);
            }
            lua_settop(state, 0);
            return result;
        }

        // 收集结果：返回值与段逐位对应
        QList<Part> parts;
        parts.reserve(compiled.segments.size());
        bool bad_value = false;
        QString bad_detail;
        for (int i = 0; i < segment_count; ++i) {
            const Segment& segment = compiled.segments.at(i);
            Part part;
            if (segment.kind == Segment::Kind::Literal) {
                bool ok = false;
                part.text = value_to_qstring(state, i + 1, &ok);
            } else if (lua_istable(state, i + 1)) {
                part.is_list = true;
                const lua_Integer size = static_cast<lua_Integer>(lua_rawlen(state, i + 1));
                part.items.reserve(static_cast<qsizetype>(size));
                for (lua_Integer k = 1; k <= size; ++k) {
                    lua_geti(state, i + 1, k);
                    bool ok = true;
                    part.items.append(value_to_qstring(state, -1, &ok));
                    lua_pop(state, 1);
                }
            } else {
                bool ok = true;
                part.text = value_to_qstring(state, i + 1, &ok);
                if (!ok) {
                    bad_value = true;
                    bad_detail = QStringLiteral("区段 `$%1$` 的结果是 %2，无法作为文本")
                                         .arg(segment.text,
                                              QString::fromUtf8(
                                                      lua_typename(state, lua_type(state, i + 1))));
                }
            }
            parts.append(part);
        }
        const bool row_context_used = box.row_context_used();
        lua_settop(state, 0);

        // 逃逸面 3：脚本用 pcall 把中止错误吞掉时，这一次调用会**正常返回**。
        // 所以成功路径也必须检查宿主侧的违规标记，否则「吞掉即逃脱」。
        if (box.violation() != sandbox::Violation::None) {
            result.error = QStringLiteral("已中止：%1").arg(box.violation_message());
            return result;
        }

        if (bad_value) {
            result.error = bad_detail;
            return result;
        }

        // 行展开（技术方案 §3.2）：空列表 → 跳过该行；长度 1 → 视作标量
        qsizetype multiplicity = 1;
        bool skipped = false;
        for (const Part& part : parts) {
            if (!part.is_list) {
                continue;
            }
            if (part.items.isEmpty()) {
                skipped = true;
                break;
            }
            if (part.items.size() > 1) {
                if (part.items.size() > limits.max_rows / std::max<qsizetype>(multiplicity, 1)) {
                    result.error = QStringLiteral("展开后行数超过上限（%1）").arg(limits.max_rows);
                    return result;
                }
                multiplicity *= part.items.size();
            }
        }
        if (skipped) {
            break;  // 该行被跳过；同一模板的后续行也一样，直接结束
        }
        if (rows.size() + multiplicity > limits.max_rows) {
            result.error = QStringLiteral("展开后行数超过上限（%1）").arg(limits.max_rows);
            return result;
        }

        // 混合进制展开：左者为外层、右者为内层 ⇒ 权重从右往左累积
        QList<qsizetype> strides(parts.size(), 1);
        qsizetype accumulator = 1;
        for (qsizetype i = parts.size(); i-- > 0;) {
            const Part& part = parts.at(i);
            const qsizetype size = (part.is_list && part.items.size() > 1) ? part.items.size() : 1;
            strides[i] = accumulator;
            accumulator *= size;
        }

        for (qsizetype k = 0; k < multiplicity; ++k) {
            QString text;
            for (qsizetype i = 0; i < parts.size(); ++i) {
                const Part& part = parts.at(i);
                if (!part.is_list) {
                    text += part.text;
                } else if (part.items.size() == 1) {
                    text += part.items.first();
                } else {
                    text += part.items.at((k / strides.at(i)) % part.items.size());
                }
            }
            rows.append(text);
        }

        // 模板没用到行上下文 ⇒ 再求值只会得到同样的结果，到此为止
        if (!row_context_used) {
            break;
        }
    }

    result.row_count = total_rows;
    result.rows = rows;
    return result;
}

}  // namespace batchsmith::core::dsl
