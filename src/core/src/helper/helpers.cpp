#include "batchsmith/core/helper/helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include <QRegularExpression>
#include <QStringList>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "batchsmith/core/sandbox/sandbox.hpp"
#include "batchsmith/core/text/natural_compare.hpp"

namespace batchsmith::core::helper {

namespace {

using sandbox::Sandbox;

/// 栈深度守卫：进入时记下深度，作用域结束时若不一致就当场炸掉。
///
/// 这不是洁癖 —— patch_string_and_table_functions 曾经就写成了 `lua_pop(state, 2)`，
/// 多弹一层把刚建好的 env 弹掉了，后续元表与注册表里存的都不是 env，
/// **症状是求值直接卡死**而不是报错。这类错必须当场暴露。
class StackGuard {
public:
    StackGuard(lua_State* state, const char* where)
        : m_state(state), m_base(lua_gettop(state)), m_where(where) {}

    ~StackGuard() {
        const int now = lua_gettop(m_state);
        if (now != m_base) {
            std::fprintf(stderr,
                         "[batchsmith] 栈深度不一致：%s 入口 %d，出口 %d\n",
                         m_where,
                         m_base,
                         now);
            std::abort();
        }
    }

    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;

private:
    lua_State* m_state;
    int m_base;
    const char* m_where;
};

constexpr const char* kEnvKey = "batchsmith.env";
constexpr const char* kListMetaKey = "batchsmith.list_metatable";
constexpr const char* kOrigRepKey = "batchsmith.orig.string.rep";
constexpr const char* kOrigFormatKey = "batchsmith.orig.string.format";
constexpr const char* kOrigConcatKey = "batchsmith.orig.table.concat";

qsizetype max_string_bytes(lua_State* state) {
    return Sandbox::from_state(state)->limits().max_string_bytes;
}

// ---------------------------------------------------------------------------
// 取值 / 压值
// ---------------------------------------------------------------------------

/// 标量 → QString。表/函数等非标量返回 false（由调用方给出可读的报错）。
bool to_qstring(lua_State* state, int index, QString* out) {
    switch (lua_type(state, index)) {
        case LUA_TNIL:
            *out = QString();
            return true;
        case LUA_TBOOLEAN:
            *out = lua_toboolean(state, index) != 0 ? QStringLiteral("true")
                                                    : QStringLiteral("false");
            return true;
        case LUA_TNUMBER:
        case LUA_TSTRING: {
            size_t length = 0;
            const char* text = lua_tolstring(state, index, &length);
            *out = QString::fromUtf8(text, static_cast<qsizetype>(length));
            return true;
        }
        default:
            return false;
    }
}

QString require_string(lua_State* state, int index, const char* function) {
    QString value;
    if (!to_qstring(state, index, &value)) {
        luaL_error(state, "%s: 第 %d 个参数只能是字符串/数字/布尔", function, index);
    }
    return value;
}

/// 列表参数：接受表（读 1..rawlen）或字符串（视作单元素列表）
QStringList list_arg(lua_State* state, int index, const char* function) {
    const int type = lua_type(state, index);
    if (type == LUA_TTABLE) {
        QStringList items;
        const lua_Integer size = static_cast<lua_Integer>(lua_rawlen(state, index));
        items.reserve(static_cast<qsizetype>(size));
        for (lua_Integer i = 1; i <= size; ++i) {
            lua_geti(state, index, i);  // 走 __index，缺省方式因此生效
            QString value;
            if (!to_qstring(state, -1, &value)) {
                lua_pop(state, 1);
                luaL_error(state, "%s: 列表第 %d 项不是标量", function, static_cast<int>(i));
            }
            items.append(value);
            lua_pop(state, 1);
        }
        return items;
    }
    if (type == LUA_TSTRING || type == LUA_TNUMBER) {
        return QStringList{require_string(state, index, function)};
    }
    luaL_error(state, "%s: 需要一个列表（表）作为参数", function);
    return {};
}

/// 压入一个列表：值在 1..n，并挂上「越界返回空串」的元表（与 ADR-8 的约定一致）。
/// 刻意**不设 `__len`** —— `#list` 必须等于原始长度。
void push_list(lua_State* state, const QStringList& items) {
    lua_newtable(state);
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QByteArray utf8 = items.at(i).toUtf8();
        lua_pushlstring(state, utf8.constData(), static_cast<size_t>(utf8.size()));
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kListMetaKey);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushcfunction(state, [](lua_State* inner) -> int {
            lua_pushliteral(inner, "");  // 越界一律空串，不返回 nil
            return 1;
        });
        lua_setfield(state, -2, "__index");
        lua_pushvalue(state, -1);
        lua_setfield(state, LUA_REGISTRYINDEX, kListMetaKey);
    }
    lua_setmetatable(state, -2);
}

void push_string(lua_State* state, const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    lua_pushlstring(state, utf8.constData(), static_cast<size_t>(utf8.size()));
}

qsizetype integer_arg(lua_State* state, int index, const char* function) {
    if (!lua_isinteger(state, index) && !lua_isnumber(state, index)) {
        luaL_error(state, "%s: 第 %d 个参数需要整数", function, index);
    }
    return static_cast<qsizetype>(lua_tointeger(state, index));
}

/// 把 helper 表注册进 env 的便捷写法
struct Helpers {
    const char* name;
    lua_CFunction function;
};

// ---------------------------------------------------------------------------
// 列表类
// ---------------------------------------------------------------------------

int helper_count(lua_State* state) {
    luaL_checkany(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(lua_rawlen(state, 1)));
    return 1;
}

int helper_index(lua_State* state) {
    luaL_checkany(state, 1);
    const qsizetype index = integer_arg(state, 2, "index");
    lua_geti(state, 1, static_cast<lua_Integer>(index));  // 走 __index：缺省方式生效
    return 1;
}

int helper_tolist(lua_State* state) {
    const QStringList items = list_arg(state, 1, "tolist");
    push_list(state, items);
    return 1;
}

int helper_slice(lua_State* state) {
    const QStringList items = list_arg(state, 1, "slice");
    qsizetype from = integer_arg(state, 2, "slice");
    qsizetype to = lua_gettop(state) >= 3 ? integer_arg(state, 3, "slice") : items.size();

    from = std::max<qsizetype>(from, 1);
    to = std::min<qsizetype>(to, items.size());

    QStringList result;
    for (qsizetype i = from; i <= to && i <= items.size(); ++i) {
        result.append(items.at(i - 1));
    }
    push_list(state, result);
    return 1;
}

int helper_concat(lua_State* state) {
    const QStringList items = list_arg(state, 1, "concat");
    const QString separator =
            lua_gettop(state) >= 2 ? require_string(state, 2, "concat") : QString();
    const QString joined = items.join(separator);
    if (joined.size() > max_string_bytes(state)) {
        luaL_error(state, "concat: 结果超过单次字符串上限");
    }
    push_string(state, joined);
    return 1;
}

int helper_sort(lua_State* state) {
    QStringList items = list_arg(state, 1, "sort");
    std::ranges::sort(items);
    push_list(state, items);
    return 1;
}

int helper_natsort(lua_State* state) {
    QStringList items = list_arg(state, 1, "natsort");
    std::ranges::sort(items, [](const QString& left, const QString& right) {
        return batchsmith::core::natural_less(left, right);
    });
    push_list(state, items);
    return 1;
}

int helper_uniq(lua_State* state) {
    const QStringList items = list_arg(state, 1, "uniq");
    QStringList result;
    for (const QString& item : items) {
        if (!result.contains(item)) {
            result.append(item);
        }
    }
    push_list(state, result);
    return 1;
}

// ---------------------------------------------------------------------------
// 组合类
// ---------------------------------------------------------------------------

int helper_matrix(lua_State* state) {
    const QStringList left = list_arg(state, 1, "matrix");
    const QStringList right = list_arg(state, 2, "matrix");
    const QString separator =
            lua_gettop(state) >= 3 ? require_string(state, 3, "matrix") : QStringLiteral("-");

    const qsizetype total = left.size() * right.size();
    if (total > Sandbox::from_state(state)->limits().max_rows) {
        luaL_error(state, "matrix: 组合数超过上限");
    }
    if (total > max_string_bytes(state)) {
        luaL_error(state, "matrix: 结果总量超过单次字符串上限");
    }

    QStringList result;
    result.reserve(total);
    for (const QString& a : left) {       // 左列表 = 外层
        for (const QString& b : right) {  // 右列表 = 内层
            result.append(a + separator + b);
        }
    }
    push_list(state, result);
    return 1;
}

int helper_zip(lua_State* state) {
    const QStringList left = list_arg(state, 1, "zip");
    const QStringList right = list_arg(state, 2, "zip");
    const QString separator =
            lua_gettop(state) >= 3 ? require_string(state, 3, "zip") : QStringLiteral("-");

    const qsizetype total = std::max(left.size(), right.size());
    QStringList result;
    result.reserve(total);
    for (qsizetype i = 0; i < total; ++i) {
        // 短的按空串补位（与「补位」的整体约定一致），不丢项
        result.append(left.value(i) + separator + right.value(i));
    }
    push_list(state, result);
    return 1;
}

// ---------------------------------------------------------------------------
// 生成类
// ---------------------------------------------------------------------------

int helper_seq(lua_State* state) {
    const qsizetype first = integer_arg(state, 1, "seq");
    qsizetype from = 1;
    qsizetype to = first;
    qsizetype step = 1;

    if (lua_gettop(state) >= 2) {
        from = first;
        to = integer_arg(state, 2, "seq");
        step = lua_gettop(state) >= 3 ? integer_arg(state, 3, "seq") : 1;
    }
    if (step == 0) {
        luaL_error(state, "seq: 步长不能为 0");
    }

    QStringList result;
    if (step > 0) {
        for (qsizetype i = from; i <= to; i += step) {
            result.append(QString::number(i));
            if (result.size() > Sandbox::from_state(state)->limits().max_rows) {
                luaL_error(state, "seq: 长度超过上限");
            }
        }
    } else {
        for (qsizetype i = from; i >= to; i += step) {
            result.append(QString::number(i));
            if (result.size() > Sandbox::from_state(state)->limits().max_rows) {
                luaL_error(state, "seq: 长度超过上限");
            }
        }
    }
    push_list(state, result);
    return 1;
}

/// 沙箱自己的随机源：**固定种子**，保证预览与实际执行一致（技术方案 §3.5 的 seed 条目）
int helper_seed(lua_State* state) {
    Sandbox* sandbox = Sandbox::from_state(state);
    sandbox->rng().seed(static_cast<unsigned long long>(integer_arg(state, 1, "seed")));
    return 0;
}

int random_between(lua_State* state, qsizetype low, qsizetype high) {
    if (high < low) {
        std::swap(low, high);
    }
    std::uniform_int_distribution<qsizetype> distribution(low, high);
    lua_pushinteger(state,
                    static_cast<lua_Integer>(distribution(Sandbox::from_state(state)->rng())));
    return 1;
}

int helper_rand(lua_State* state) {
    const int argc = lua_gettop(state);
    if (argc == 0) {
        std::uniform_real_distribution<double> distribution(0.0, 1.0);
        lua_pushnumber(state, distribution(Sandbox::from_state(state)->rng()));
        return 1;
    }
    if (argc == 1) {
        return random_between(state, 1, integer_arg(state, 1, "rand"));  // rand(n) = 1..n
    }
    return random_between(state, integer_arg(state, 1, "rand"), integer_arg(state, 2, "rand"));
}

/// 正则：返回捕获列表。有捕获组 → 各组；无捕获组 → 整个匹配（单元素）；不匹配 → 空列表。
///
/// 第三个参数（可选）直接取列表里的第 n 项 —— 这是最常见的用法：
/// `regex(名字, [[第(\d+)话]], 1)` 比 `regex(名字, [[第(\d+)话]])[1]` 少一层下标，
/// 而且**越界一律空串**（与列表越界的约定一致），不会像取下标那样拿到 nil。
int helper_regex(lua_State* state) {
    const QString subject = require_string(state, 1, "regex");
    const QString pattern = require_string(state, 2, "regex");

    const QRegularExpression expression(pattern);
    if (!expression.isValid()) {
        luaL_error(state, "regex: 正则无效：%s", expression.errorString().toUtf8().constData());
    }

    QStringList result;
    const QRegularExpressionMatch match = expression.match(subject);
    if (match.hasMatch()) {
        const int captures = expression.captureCount();
        if (captures <= 0) {
            result.append(match.captured(0));
        } else {
            for (int i = 1; i <= captures; ++i) {
                result.append(match.captured(i));
            }
        }
    }

    if (lua_gettop(state) < 3) {
        push_list(state, result);
        return 1;
    }

    const qsizetype which = integer_arg(state, 3, "regex");
    push_string(state, which >= 1 && which <= result.size() ? result.at(which - 1) : QString());
    return 1;
}

// ---------------------------------------------------------------------------
// 文本类
// ---------------------------------------------------------------------------

int helper_fmt(lua_State* state) {
    // 转交给经过预检的 string.format（先算规模再分配，ADR-6 逃逸面 1）
    lua_getfield(state, LUA_REGISTRYINDEX, kEnvKey);
    lua_getfield(state, -1, "string");
    lua_getfield(state, -1, "format");
    lua_remove(state, -3);
    lua_remove(state, -2);
    lua_insert(state, 1);  // 把 format 放到参数最前面
    if (lua_pcall(state, lua_gettop(state) - 1, 1, 0) != LUA_OK) {
        return lua_error(state);
    }
    return 1;
}

int helper_upper(lua_State* state) {
    push_string(state, require_string(state, 1, "upper").toUpper());
    return 1;
}

int helper_lower(lua_State* state) {
    push_string(state, require_string(state, 1, "lower").toLower());
    return 1;
}

int helper_trim(lua_State* state) {
    push_string(state, require_string(state, 1, "trim").trimmed());
    return 1;
}

/// 文本替换：**按字面**找 `find` 并全部换成 `to`。
///
/// 「字面」是刻意的，也是与正则版本的分工：批量改名时绝大多数要换的是普通子串
/// （`第` → `第`、`_final` → ``），而 `.`、`(`、`[` 这些字符在正则里有特殊含义 ——
/// 让 `replace` 认正则，等于要求用户每写一个字面串都要先想"这里面有没有元字符"。
///
/// ⚠️ 0.3.0 的 `replace` 是**正则**替换，这里改成了字面替换（正则版见 `resub`）。
/// 老预设里 `replace(x, "a+", "b")` 的含义会从"一个或多个 a"变成"字面 a+"，
/// 结果通常是不再匹配 —— 这是**破坏性变更**，见 CHANGELOG。
int helper_replace(lua_State* state) {
    QString subject = require_string(state, 1, "replace");  // replace 会就地修改
    const QString find = require_string(state, 2, "replace");
    const QString replacement = require_string(state, 3, "replace");

    if (find.isEmpty()) {
        // 空串在字面替换里是"在每个位置插入"，Qt 会把每个字符之间都插一遍 ——
        // 几乎不会是用户想要的，明确报错比给一个吓人的结果好
        luaL_error(state, "replace: 要替换的文本不能为空（要按正则替换用 resub）");
    }
    push_string(state, subject.replace(find, replacement));  // 全局替换
    return 1;
}

/// 正则替换：全局替换，`to` 里可用 `\1`…`\9` 引用捕获组。
int helper_resub(lua_State* state) {
    QString subject = require_string(state, 1, "resub");  // resub 会就地修改
    const QString pattern = require_string(state, 2, "resub");
    const QString replacement = require_string(state, 3, "resub");

    const QRegularExpression expression(pattern);
    if (!expression.isValid()) {
        luaL_error(state, "resub: 正则无效：%s", expression.errorString().toUtf8().constData());
    }
    push_string(state, subject.replace(expression, replacement));  // 全局替换
    return 1;
}

int helper_match(lua_State* state) {
    const QString subject = require_string(state, 1, "match");
    const QString pattern = require_string(state, 2, "match");

    const QRegularExpression expression(pattern);
    if (!expression.isValid()) {
        luaL_error(state, "match: 正则无效");
    }
    const QRegularExpressionMatch match = expression.match(subject);
    push_string(state, match.hasMatch() ? match.captured(0) : QString());
    return 1;
}

int helper_pad(lua_State* state) {
    const QString text = require_string(state, 1, "pad");
    const qsizetype width = integer_arg(state, 2, "pad");
    const QString fill =
            lua_gettop(state) >= 3 ? require_string(state, 3, "pad") : QStringLiteral(" ");

    QString result = text;
    if (fill.isEmpty()) {
        luaL_error(state, "pad: 填充串不能为空");
    }
    // 右侧补齐；不足一个填充串宽度时按其前缀补
    while (result.size() < width) {
        const qsizetype need = width - result.size();
        result += need >= fill.size() ? fill : fill.left(need);
    }
    if (result.size() > max_string_bytes(state)) {
        luaL_error(state, "pad: 结果超过单次字符串上限");
    }
    push_string(state, result);
    return 1;
}

// ---------------------------------------------------------------------------
// 路径类（同时接受 / 与 \，输出按 / 归一化）
// ---------------------------------------------------------------------------

QString normalized(const QString& path) {
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return result;
}

/// 一个路径拆成「主干」与「扩展名」两部分。
///
/// ⚠️ **「什么算扩展名」这条判定只允许在这里写一次**。`ext` / `stem` / `set_ext` /
/// `add_ext` / `add_suffix` 全部依赖它 —— 以前 `ext` 与 `stem` 各自抄了一遍
/// `lastIndexOf` 的算法，那种重复在加第三个函数时必然分叉。
struct NameParts {
    QString stem;          ///< 去过扩展名的部分（**含目录**）
    QString ext;           ///< 扩展名，不含点
    bool has_ext = false;  ///< `a.` 算「有扩展名，但扩展名为空」，与 `a` 不同
};

/// 判定规则（三条，都写死在这里）：
///
/// 1. 扩展名取**最后一个**点之后的部分；
/// 2. 那个点必须在**最后一个 `/` 之后、且不是文件名的首字符** ——
///    所以 `.bashrc` 没有扩展名（点开头是隐藏文件，不是后缀）；
/// 3. 点前面必须至少有一个字符 ——`a/b.c/d` 里的点是**目录名**的一部分，不算。
[[nodiscard]] NameParts split_name(const QString& path) {
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot > slash + 1) {
        return NameParts{path.left(dot), path.mid(dot + 1), true};
    }
    return NameParts{path, QString(), false};
}

/// 把「扩展名」参数归一化：容忍前导点（`".mkv"` 与 `"mkv"` 同义），
/// 全是点或空串则视作「没有扩展名」。
[[nodiscard]] QString clean_ext(const QString& ext) {
    QString result = ext;
    while (result.startsWith(QLatin1Char('.'))) {
        result.remove(0, 1);
    }
    return result;
}

/// 压值 + 单次字符串上限检查。这四个文件名 helper 的结果都可能比输入长
/// （`add_ext` 追加、`safe_name` 的替换串可能多字符），所以要挡一道。
void push_checked(lua_State* state, const QString& text, const char* function) {
    if (text.size() > max_string_bytes(state)) {
        luaL_error(state, "%s: 结果超过单次字符串上限", function);
    }
    push_string(state, text);
}

int helper_basename(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "basename"));
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    push_string(state, slash < 0 ? path : path.mid(slash + 1));
    return 1;
}

int helper_dirname(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "dirname"));
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    push_string(state, slash <= 0 ? QString() : path.left(slash));
    return 1;
}

int helper_ext(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "ext"));
    push_string(state, split_name(path).ext);  // 不含点；没有扩展名则空串
    return 1;
}

int helper_stem(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "stem"));
    push_string(state, split_name(path).stem);
    return 1;
}

int helper_join(lua_State* state) {
    const int argc = lua_gettop(state);
    QString result;
    for (int i = 1; i <= argc; ++i) {
        QString part = normalized(require_string(state, i, "join"));
        if (part.isEmpty()) {
            continue;
        }
        if (result.isEmpty()) {
            result = part;
            continue;
        }
        while (part.startsWith(QLatin1Char('/'))) {
            part.remove(0, 1);
        }
        if (!result.endsWith(QLatin1Char('/'))) {
            result += QLatin1Char('/');
        }
        result += part;
    }
    push_string(state, result);
    return 1;
}

// ---------------------------------------------------------------------------
// 文件名类 —— 直接改名字的那几个（扩展名的增删换、文件名安全化）
//
// 与「路径类」的分工：路径类只**拆**（basename / dirname / ext / stem / join），
// 这里负责**改**。批量改名时真正高频的正是这几个动作，而用现有的拆解函数拼一遍
// 很容易写错（漏掉「本来就没有扩展名」的分支就多出一个点）。
// ---------------------------------------------------------------------------

/// 换扩展名（替换，不是追加。追加用 `add_ext`）。
///
/// 保留目录部分，只动最后一个点之后的东西。三条边界：
/// - 本来没有扩展名 → 直接接上（`a` + `mkv` → `a.mkv`），**不会**变成 `a..mkv`；
/// - `e` 给空串（或只有点）→ 去掉扩展名（`a/b/c.txt` → `a/b/c`）；
/// - `e` 的前导点可有可无，`".mkv"` 与 `"mkv"` 结果相同。
int helper_set_ext(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "set_ext"));
    const QString ext = clean_ext(require_string(state, 2, "set_ext"));

    const NameParts parts = split_name(path);
    push_checked(
            state, ext.isEmpty() ? parts.stem : parts.stem + QLatin1Char('.') + ext, "set_ext");
    return 1;
}

/// 在**末尾**追加一段当作扩展名（不动原来的扩展名）。
///
/// `a.mkv` + `bak` → `a.mkv.bak`。备份/二次加工那种「加一层后缀」的场合用它。
/// `e` 为空则原样返回。
int helper_add_ext(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "add_ext"));
    const QString ext = clean_ext(require_string(state, 2, "add_ext"));

    push_checked(state, ext.isEmpty() ? path : path + QLatin1Char('.') + ext, "add_ext");
    return 1;
}

/// 在扩展名**之前**插入一段文本（目录与扩展名都不动）。
///
/// `a.mkv` + `_final` → `a_final.mkv`；没有扩展名时接在末尾（`a` → `a_final`）。
///
/// 这是「给一整批文件加同一个标记」最常用的动作。用 `replace` 做不到：
/// `replace(p, "a", "b")` 会把**目录名里的** a 也换掉。
int helper_add_suffix(lua_State* state) {
    const QString path = normalized(require_string(state, 1, "add_suffix"));
    const QString suffix = require_string(state, 2, "add_suffix");

    if (suffix.isEmpty()) {
        push_checked(state, path, "add_suffix");
        return 1;
    }

    const NameParts parts = split_name(path);
    if (!parts.has_ext) {
        push_checked(state, path + suffix, "add_suffix");
        return 1;
    }
    push_checked(state, parts.stem + suffix + QLatin1Char('.') + parts.ext, "add_suffix");
    return 1;
}

/// 设备名：Windows 上这些名字（不分大小写）**永远不能**作为文件主干名，
/// 哪怕带扩展名也不行（`con.mkv` 一样建不出来）。
[[nodiscard]] bool is_reserved_device_name(const QString& base) {
    static const QStringList kReserved = {
            QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
            QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
            QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
            QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
            QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
            QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
            QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
            QStringLiteral("LPT9"),
    };
    return kReserved.contains(base.toUpper());
}

/// 把一段文本变成**能真正落盘**的文件名。
///
/// 做三件事，都是「不做的话会静默出错」的那一类：
///
/// 1. **换掉非法字符**：Windows 不允许 `< > : " / \ | ? *` 与控制字符（0x00–0x1F），
///    默认换成 `_`（`repl` 可改）。番剧标题里的 `第1话 序章: 起点` 直接当文件名
///    在 Windows 上会被建造成别的东西 —— 而 Linux 上却能成功，于是同一个预设
///    在两台机器上行为不同。
/// 2. **去掉结尾的点和空格**：Windows 会**静默截断**它们（`a.` 建成 `a`、`"a "` 建成 `a`）。
///    截断之后名字与计划里写的不一样，而用户看到的"成功"是真的 —— 这是最难查的一类。
/// 3. **躲开设备名**：`con` / `nul` / `com1`… 在主干名后补 `_`（`con.mkv` → `con_.mkv`）。
///
/// ⚠️ **整串当作一个名字，不做路径拆分**。给了含 `/` 的路径，那些斜杠也会被当成
/// 非法字符换掉（`D:/x/a.mkv` → `D__x_a.mkv`）。要保留目录就自己拼：
/// `join(dirname(p), safe_name(basename(p)))`。
/// 这样定是刻意的：它的输入是"标题/字段"这类**名字**，猜路径会把
/// `第1话/第2话` 这种本当整串处理的输入拆错，而拆错是静默的。
///
/// 结果为空串是可能的（输入全是非法字符与结尾点），此时**不补默认名** ——
/// 交给计划阶段报 `TargetEmpty`，比悄悄生成 `untitled` 之类诚实。
int helper_safe_name(lua_State* state) {
    const QString text = require_string(state, 1, "safe_name");
    const QString replacement =
            lua_gettop(state) >= 2 ? require_string(state, 2, "safe_name") : QStringLiteral("_");

    static const QString kIllegal = QStringLiteral("<>:\"/\\|?*");

    QString sanitized;
    sanitized.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.unicode() < 0x20 || kIllegal.contains(ch)) {
            sanitized += replacement;
        } else {
            sanitized += ch;
        }
    }

    // 结尾的点与空格 Windows 会截掉 —— 提前去掉，让计划里写的就是最终的名字
    while (!sanitized.isEmpty() &&
           (sanitized.endsWith(QLatin1Char('.')) || sanitized.endsWith(QLatin1Char(' ')))) {
        sanitized.chop(1);
    }

    const NameParts parts = split_name(sanitized);
    if (is_reserved_device_name(parts.stem)) {
        const QString base = parts.stem + QLatin1Char('_');
        sanitized = parts.has_ext ? base + QLatin1Char('.') + parts.ext : base;
    }

    push_checked(state, sanitized, "safe_name");
    return 1;
}

// ---------------------------------------------------------------------------
// 类型类
// ---------------------------------------------------------------------------

int helper_num(lua_State* state) {
    const QString text = require_string(state, 1, "num").trimmed();
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (ok) {
        // 整数值压整数：Lua 5.4 里整数与浮点的 tostring 不同（43 vs 43.0），
        // 而 DSL 的输出是给人看的文本，多一个 .0 很扎眼
        if (std::floor(value) == value && std::abs(value) < 9.0e18) {
            lua_pushinteger(state, static_cast<lua_Integer>(value));
        } else {
            lua_pushnumber(state, value);
        }
        return 1;
    }
    const lua_Integer integer = static_cast<lua_Integer>(text.toLongLong(&ok));
    if (ok) {
        lua_pushinteger(state, integer);
        return 1;
    }
    lua_pushnil(state);  // 解析不出来 → nil（宿主会当作空串）
    return 1;
}

int helper_str(lua_State* state) {
    const QString value = require_string(state, 1, "str");
    push_string(state, value);
    return 1;
}

// ---------------------------------------------------------------------------
// 逃逸面处理：先分配后检查的三个函数（ADR-6 逃逸面 1）
// ---------------------------------------------------------------------------

int checked_string_rep(lua_State* state) {
    const QString text = require_string(state, 1, "string.rep");
    const qsizetype count = std::max<qsizetype>(integer_arg(state, 2, "string.rep"), 0);
    const QString separator =
            lua_gettop(state) >= 3 ? require_string(state, 3, "string.rep") : QString();

    // 先算规模再分配：等内存限制生效时，分配已经发生了
    const qsizetype estimated =
            text.size() * count + separator.size() * std::max<qsizetype>(count - 1, 0);
    if (estimated > max_string_bytes(state)) {
        luaL_error(state,
                   "string.rep: 结果约 %d 字节，超过单次字符串上限",
                   static_cast<int>(estimated));
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kOrigRepKey);
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 1);
    return 1;
}

int checked_string_format(lua_State* state) {
    const QString format = require_string(state, 1, "string.format");

    // 扫一遍格式串：宽度/精度出现超大数字就先拦下来（%99999999d 这类）
    static const QRegularExpression big_width(QStringLiteral("%[-+ #0]*([0-9]{7,})"));
    const QRegularExpressionMatch match = big_width.match(format);
    if (match.hasMatch()) {
        luaL_error(state, "string.format: 宽度过大");
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kOrigFormatKey);
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 1);
    return 1;
}

int checked_table_concat(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    const lua_Integer size = static_cast<lua_Integer>(lua_rawlen(state, 1));

    qsizetype separator_size = 0;
    if (lua_type(state, 2) == LUA_TSTRING) {
        size_t separator_length = 0;
        lua_tolstring(state, 2, &separator_length);
        separator_size = static_cast<qsizetype>(separator_length);
    }

    qsizetype total = separator_size * std::max<lua_Integer>(size - 1, 0);  // 分隔符也要算进去
    for (lua_Integer i = 1; i <= size; ++i) {
        lua_geti(state, 1, i);
        size_t length = 0;
        if (lua_tolstring(state, -1, &length) != nullptr) {
            total += static_cast<qsizetype>(length);
        } else if (lua_type(state, -1) == LUA_TNUMBER) {
            total += 24;  // 数字转字符串后的粗估
        } else if (lua_type(state, -1) != LUA_TNIL) {
            lua_pop(state, 1);
            return luaL_error(state, "table.concat: 表里有非字符串/数字的项");
        }
        lua_pop(state, 1);
        if (total > max_string_bytes(state)) {
            return luaL_error(state, "table.concat: 结果超过单次字符串上限");
        }
    }

    lua_getfield(state, LUA_REGISTRYINDEX, kOrigConcatKey);
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, 1);
    return 1;
}

constexpr Helpers kHelpers[] = {
        // 列表
        {"count", helper_count},
        {"index", helper_index},
        {"tolist", helper_tolist},
        {"slice", helper_slice},
        {"concat", helper_concat},
        {"sort", helper_sort},
        {"natsort", helper_natsort},
        {"uniq", helper_uniq},
        // 组合
        {"matrix", helper_matrix},
        {"zip", helper_zip},
        // 生成
        {"seq", helper_seq},
        {"rand", helper_rand},
        {"seed", helper_seed},
        {"regex", helper_regex},
        // 文本
        {"fmt", helper_fmt},
        {"upper", helper_upper},
        {"lower", helper_lower},
        {"trim", helper_trim},
        {"replace", helper_replace},
        {"resub", helper_resub},
        {"match", helper_match},
        {"pad", helper_pad},
        // 路径
        {"basename", helper_basename},
        {"dirname", helper_dirname},
        {"ext", helper_ext},
        {"stem", helper_stem},
        {"join", helper_join},
        // 文件名
        {"set_ext", helper_set_ext},
        {"add_ext", helper_add_ext},
        {"add_suffix", helper_add_suffix},
        {"safe_name", helper_safe_name},
        // 类型
        {"num", helper_num},
        {"str", helper_str},
};

}  // namespace

QStringList registered_names() {
    QStringList names;
    names.reserve(static_cast<qsizetype>(std::size(kHelpers)));
    for (const Helpers& helper : kHelpers) {
        names.append(QString::fromLatin1(helper.name));
    }
    return names;
}

void register_helpers(Sandbox& sandbox) {
    lua_State* state = sandbox.state();
    const StackGuard guard(state, "register_helpers");
    lua_getfield(state, LUA_REGISTRYINDEX, kEnvKey);  // env

    for (const Helpers& helper : kHelpers) {
        lua_pushcfunction(state, helper.function);
        lua_setfield(state, -2, helper.name);
    }

    lua_pop(state, 1);
}

void patch_string_and_table_functions(Sandbox& sandbox) {
    lua_State* state = sandbox.state();
    const StackGuard guard(state, "patch_string_and_table_functions");

    lua_getfield(state, LUA_REGISTRYINDEX, kEnvKey);  // [env]

    // ⚠️ 每一张子表用完**立刻弹出**，不要留到后面再弹。
    // 踩过的坑：写成"先取 string 打补丁、再对栈顶取 math"，那条 `-1` 索引的就是
    // 还留在栈顶的 string 表 → 取到 nil → 对 nil 设字段 → 未保护错误 → 进程 abort()。
    // 教训：C API 里 `-1` 到底是谁，必须靠"用完即弹"保证，不能靠人眼数栈。

    // string.rep / string.format
    lua_getfield(state, -1, "string");
    lua_getfield(state, -1, "rep");
    lua_setfield(state, LUA_REGISTRYINDEX, kOrigRepKey);
    lua_pushcfunction(state, checked_string_rep);
    lua_setfield(state, -2, "rep");

    lua_getfield(state, -1, "format");
    lua_setfield(state, LUA_REGISTRYINDEX, kOrigFormatKey);
    lua_pushcfunction(state, checked_string_format);
    lua_setfield(state, -2, "format");
    lua_pop(state, 1);  // string

    // math.random / math.randomseed → 沙箱自己的可复现随机源
    lua_getfield(state, -1, "math");
    lua_pushcfunction(state, helper_rand);
    lua_setfield(state, -2, "random");
    lua_pushcfunction(state, helper_seed);
    lua_setfield(state, -2, "randomseed");
    lua_pop(state, 1);  // math

    // table.concat
    lua_getfield(state, -1, "table");
    lua_getfield(state, -1, "concat");
    lua_setfield(state, LUA_REGISTRYINDEX, kOrigConcatKey);
    lua_pushcfunction(state, checked_table_concat);
    lua_setfield(state, -2, "concat");
    lua_pop(state, 1);  // table

    lua_pop(state, 1);  // env
}
}  // namespace batchsmith::core::helper
