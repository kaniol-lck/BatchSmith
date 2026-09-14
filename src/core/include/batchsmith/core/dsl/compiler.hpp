#pragma once

#include <QList>
#include <QString>

#include "batchsmith/core/dsl/template_scanner.hpp"

namespace batchsmith::core::dsl {

/// 编译结果：可直接交给 `luaL_loadbuffer` 的 Lua 源码。
struct CompiledTemplate {
    /// 形如 `return function(i) local s1 = (...) return "lit", s1 end`
    QString lua_source;

    /// 扫描出的段序列 —— **与每次调用的返回值逐位对应**，宿主据此判断哪一段是列表、
    /// 哪一段是字面文本（因此不必再扫一遍模板）。
    QList<Segment> segments;

    /// 区段个数（等于 `segments` 里 Section 的数量）
    int section_count = 0;

    QString error;  ///< 非空即编译失败

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// DSL 模板 → Lua 源码（技术方案 §3.1：**编译一次，产出可复用的 Lua 函数**）。
///
/// 生成的 chunk 形如：
///
/// ```lua
/// return function()
///   local s1 = (<区段1>)
///   local s2 = (<区段2>)
///   return "<字面1>", s1, "<字面2>", s2, "<字面3>"
/// end
/// ```
///
/// 四个要点：
///
/// 0. **函数不带参数** —— `i` / `rows` 都由沙箱的 `_ENV` 提供，而不是函数参数。
///    看着像是绕远，其实是必需的：沙箱靠 env 的 `__index` 精确判断"模板到底有没有用到
///    行上下文"，据此决定「只求值一次」还是「逐行求值」（见 engine.hpp 的说明）。
///    一旦把 `i` 做成参数，它就成了局部变量，读取不再经过 `__ENV`，这个判断就失效了 ——
///    `$list1[i]$` 会只算出一行。
///
/// 1. **每个区段包在括号里**，强制截断为一个返回值 —— `string.find` 这类函数会返回多个
///    值，不截断会让「返回值个数」与「段数」对不上，后续全部错位。
/// 2. **段值原样返回给宿主，不由 Lua 拼成字符串** —— 区段求值为列表时要展开成多行
///    （技术方案 §3.2），拼成字符串就没法展开了。
/// 3. 每个区段单独占一行，**行号 = 区段序号**；Lua 报语法错时宿主据此指出是哪一个区段
///    （技术方案 §3.4：区段只允许表达式，赋值/语句编译不过）。
///
/// 字面段用自定义单行转义器（§3.3），**不用** `string.format("%q")`。
[[nodiscard]] CompiledTemplate compile_template(const QString& template_text);

/// 把文本转成**保证单行**的 Lua 字符串字面量（含首尾双引号）。
///
/// `\` 与 `"` 转义；`\n` `\r` `\t` 写成转义序列；其余不可打印字符写成 `\ddd`。
[[nodiscard]] QString escape_lua_string(const QString& text);

}  // namespace batchsmith::core::dsl
