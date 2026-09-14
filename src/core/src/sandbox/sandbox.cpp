#include "batchsmith/core/sandbox/sandbox.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "batchsmith/core/helper/helpers.hpp"

namespace batchsmith::core::sandbox {

namespace {

using Clock = std::chrono::steady_clock;

/// 分配器与钩子都通过 extraspace 里的 `Sandbox*` 拿到账本。
///
/// ⚠️ extraspace 只放得下 `sizeof(void*)` 一个指针，账本本身在 Sandbox 里
/// （见头文件里的说明：往 extraspace 多写一个字节就是堆破坏）。

/// 未保护的 Lua 错误（沙箱内部不变量被破坏时才会发生）。
///
/// 默认情况下 `lua_newstate` 没有 panic 函数，Lua 会直接 `abort()` ——
/// 进程退出码 3、**一句错误都不打**，排查成本极高（这次就为此多花了几轮）。
/// 装上这个处理器，至少先把 Lua 的报错打到 stderr 上。
int panic_handler(lua_State* state) {
    const char* message = lua_tostring(state, -1);
    Sandbox* sandbox = Sandbox::from_state(state);
    std::fprintf(stderr,
                 "[batchsmith] 沙箱内部未保护错误（阶段：%s）：%s\n",
                 sandbox != nullptr ? sandbox->phase() : "未知",
                 message != nullptr ? message : "(无消息)");
    return 0;  // 返回后 Lua 仍然会 abort —— 这是刻意的：内部不变量破坏了，不该继续跑
}

/// 带计数与上限的分配器。超限返回 nullptr ⇒ Lua 抛 LUA_ERRMEM。
void* tracked_allocator(void* ud, void* ptr, size_t old_size, size_t new_size) {
    auto* state = static_cast<lua_State*>(ud);
    if (state == nullptr) {
        // lua_newstate 期间 ud 还是 nullptr（随后用 lua_setallocf 补上）：
        // 这一小段无法记账，直接透传 —— 不处理会解引用空指针
        if (new_size == 0) {
            std::free(ptr);
            return nullptr;
        }
        return std::realloc(ptr, new_size);
    }

    Sandbox* sandbox = Sandbox::from_state(state);
    if (sandbox == nullptr) {  // 关闭过程中：extraspace 已被清空
        if (new_size == 0) {
            std::free(ptr);
            return nullptr;
        }
        return std::realloc(ptr, new_size);
    }
    Sandbox::Account& account = sandbox->account();

    if (new_size == 0) {
        account.bytes -= static_cast<qsizetype>(old_size);
        std::free(ptr);
        return nullptr;
    }

    if (new_size > old_size) {
        const qsizetype delta = static_cast<qsizetype>(new_size - old_size);
        if (account.bytes + delta > sandbox->limits().max_memory_bytes) {
            // 返回 nullptr：内存不足必须由 Lua 以 LUA_ERRMEM 结束本次调用
            sandbox->raise(Violation::Memory,
                           QStringLiteral("内存超过上限（%1 MB）")
                                   .arg(sandbox->limits().max_memory_bytes / (1024 * 1024)));
            return nullptr;
        }
    }

    void* block = std::realloc(ptr, new_size);
    if (block != nullptr) {
        account.bytes += static_cast<qsizetype>(new_size) - static_cast<qsizetype>(old_size);
    }
    return block;
}

/// 指令数 + 墙钟，共用同一个 count 钩子（ADR-6：零额外线程）
void limit_hook(lua_State* state, lua_Debug* /*debug*/) {
    Sandbox* sandbox = Sandbox::from_state(state);
    if (sandbox == nullptr) {
        return;
    }
    Sandbox::Account& account = sandbox->account();
    account.instructions += sandbox->limits().instruction_slice;

    if (account.instructions > sandbox->limits().max_instructions) {
        sandbox->raise(
                Violation::Instructions,
                QStringLiteral("指令数超过上限（%1）").arg(sandbox->limits().max_instructions));
        luaL_error(state, "sandbox aborted: instruction limit exceeded");
        return;
    }

    if (sandbox->elapsed_ms() > sandbox->limits().max_wall_ms) {
        sandbox->raise(Violation::WallClock,
                       QStringLiteral("执行超时（%1 ms）").arg(sandbox->limits().max_wall_ms));
        luaL_error(state, "sandbox aborted: wall clock limit exceeded");
    }
}

int list_index_metamethod(lua_State* state) {
    // upvalue 1：该列表在沙箱里的下标（1 起）
    const auto list_index = static_cast<qsizetype>(lua_tointeger(state, lua_upvalueindex(1))) - 1;
    auto* sandbox = Sandbox::from_state(state);
    if (sandbox == nullptr) {
        lua_pushliteral(state, "");
        return 1;
    }

    if (!lua_isinteger(state, 2)) {
        lua_pushnil(state);
        return 1;
    }
    const qsizetype key = static_cast<qsizetype>(lua_tointeger(state, 2));  // 1 起

    const QStringList& items = sandbox->list_items(list_index);
    if (items.isEmpty()) {
        lua_pushliteral(state, "");  // 空列表：越界一律空串，不返回 nil
        return 1;
    }

    if (key >= 1 && key <= items.size()) {
        const QByteArray utf8 = items.at(key - 1).toUtf8();
        lua_pushlstring(state, utf8.constData(), static_cast<size_t>(utf8.size()));
        return 1;
    }

    if (sandbox->list_padding(list_index) == ListPadding::Repeat) {
        const qsizetype wrapped = ((key - 1) % items.size() + items.size()) % items.size();
        const QByteArray utf8 = items.at(wrapped).toUtf8();
        lua_pushlstring(state, utf8.constData(), static_cast<size_t>(utf8.size()));
        return 1;
    }

    lua_pushliteral(state, "");  // Empty / Ignore：留空
    return 1;
}

/// env 的 `__index`：提供 `i` / `rows`（并记录"用到了行上下文"），其余全局名一律 nil。
///
/// `i` / `rows` 刻意不做成 env 的普通键，就是为了让这个函数成为唯一的出口 ——
/// 宿主因此能精确知道模板是否依赖行上下文。
int env_index_metamethod(lua_State* state) {
    auto* sandbox = Sandbox::from_state(state);
    if (sandbox == nullptr || !lua_isstring(state, 2)) {
        lua_pushnil(state);
        return 1;
    }

    const char* key = lua_tostring(state, 2);

    // 名字长得像 listN 却没注入过 —— 多半是打错了。**必须报错**：
    // 在 Lua 里它只是个未定义全局名，读出来是 nil，而 nil 会被当成空串，
    // 于是 `$list9[i]$` 会静默产出一堆空行。这是本项目最忌讳的那类错误。
    const QLatin1StringView name(key);
    if (name.startsWith(QLatin1String("list"))) {
        bool numeric_suffix = name.size() > 4;
        for (qsizetype i = 4; i < name.size() && numeric_suffix; ++i) {
            const QLatin1Char ch = name.at(i);
            numeric_suffix = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        }
        if (numeric_suffix && !sandbox->list_names().contains(QString(name))) {
            return luaL_error(
                    state,
                    "没有名为 `%s` 的列表（现有：%s）",
                    key,
                    sandbox->list_names().join(QStringLiteral("、")).toUtf8().constData());
        }
    }

    if (std::strcmp(key, "i") == 0) {
        sandbox->mark_row_context_used();
        lua_pushinteger(state, static_cast<lua_Integer>(sandbox->current_row()));
        return 1;
    }
    if (std::strcmp(key, "rows") == 0) {
        sandbox->mark_row_context_used();
        lua_pushinteger(state, static_cast<lua_Integer>(sandbox->total_rows()));
        return 1;
    }

    lua_pushnil(state);  // 未白名单化的名字：nil（配合调用即报错，且信息可读）
    return 1;
}

}  // namespace

Sandbox::Sandbox(Limits limits) : m_limits(limits) {
    m_state = lua_newstate(tracked_allocator, nullptr);
    if (m_state == nullptr) {
        raise(Violation::Memory, QStringLiteral("无法创建 Lua 状态"));
        return;
    }
    // 分配器需要 lua_State 才能拿到账本，所以创建后再重设一次 ud
    lua_setallocf(m_state, tracked_allocator, m_state);
    lua_atpanic(m_state, panic_handler);

    // extraspace 只写这一个指针（大小恰好是 sizeof(void*)）
    *static_cast<Sandbox**>(lua_getextraspace(m_state)) = this;
    m_account.started = Clock::now();
    lua_sethook(m_state, limit_hook, LUA_MASKCOUNT, m_limits.instruction_slice);
    build_environment();
}

Sandbox::~Sandbox() {
    if (m_state != nullptr) {
        // 先清掉 extraspace，避免关闭过程中的分配触发我们的记账/报错
        *static_cast<Sandbox**>(lua_getextraspace(m_state)) = nullptr;
        lua_close(m_state);
    }
}

Sandbox* Sandbox::from_state(lua_State* state) {
    return *static_cast<Sandbox**>(lua_getextraspace(state));
}

qsizetype Sandbox::elapsed_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_account.started)
            .count();
}

void Sandbox::raise(Violation kind, const QString& message) {
    if (m_violation == Violation::None) {
        m_violation = kind;
        m_violation_message = message;
    }
}

void Sandbox::clear_violation() {
    m_violation = Violation::None;
    m_violation_message.clear();
}

void Sandbox::install_libraries() {
    // 基础库只放一个**白名单子集**：ADR-6 列的是「手工打开哪几张库」，
    // 但只开 string/table/math/utf8 的话，`type` / `tostring` / `pairs` / `pcall`
    // 都会缺失，DSL 会残缺到不好用。这里逐名挑选，而不是整张 base 表照搬。
    //
    // 明确**不暴露**：`print`（批处理工具没有输出通道）、`load`/`loadstring`/`dofile`/
    // `loadfile`（能加载任意代码）、`collectgarbage`（能压掉内存上限的效果）、
    // `setmetatable`/`getmetatable`（会破坏注入列表的缺省方式语义）、`_G`/`_VERSION`。
    //
    // `pcall` 保留 —— ADR-6 逃逸面 3 就是"脚本用 pcall 吞掉中止错误"，
    // 防御手段是宿主侧的独立违规标记（见 raise()），不是靠隐藏 pcall。
    static const char* const kBaseWhitelist[] = {
            "type",
            "tostring",
            "tonumber",
            "ipairs",
            "pairs",
            "next",
            "select",
            "error",
            "assert",
            "pcall",
            "xpcall",
            "rawequal",
            "rawget",
            "rawlen",
            "rawset",
    };

    // glb=0：不往真正的全局表写；chunk 的 _ENV 是我们的 env，二者互不可达
    luaL_requiref(m_state, "_G", luaopen_base, 0);
    for (const char* name : kBaseWhitelist) {
        lua_getfield(m_state, -1, name);
        if (!lua_isnil(m_state, -1)) {
            lua_setfield(m_state, -3, name);  // env[name] = base[name]
        } else {
            lua_pop(m_state, 1);
        }
    }
    lua_pop(m_state, 1);  // base 表

    // 只开这四张：string / table / math / utf8（ADR-6）。
    // glb=0 —— 不往全局表写，模块表只挂进我们自己的 env。
    luaL_requiref(m_state, LUA_STRLIBNAME, luaopen_string, 0);
    lua_setfield(m_state, -2, "string");

    luaL_requiref(m_state, LUA_TABLIBNAME, luaopen_table, 0);
    lua_setfield(m_state, -2, "table");

    luaL_requiref(m_state, LUA_MATHLIBNAME, luaopen_math, 0);
    lua_setfield(m_state, -2, "math");

    luaL_requiref(m_state, LUA_UTF8LIBNAME, luaopen_utf8, 0);
    lua_setfield(m_state, -2, "utf8");
}

void Sandbox::patch_unsafe_functions() {
    helper::patch_string_and_table_functions(*this);
}

void Sandbox::build_environment() {
    if (m_state == nullptr) {
        return;
    }
    lua_newtable(m_state);  // env 表（栈：env）

    // ⚠️ 立刻写进注册表：下面 install_libraries / register_helpers / patch 都是通过
    // **注册表**取 env 的。晚一步写（曾经写在函数末尾）它们就会取到 nil，
    // 对 nil 设字段会抛未保护的 Lua 错误 —— 整个进程 abort()，退出码 3，
    // 而且**看不到任何 Lua 报错**。这类"顺序依赖"必须写死在注释里。
    lua_pushvalue(m_state, -1);  // [env, env]
    lua_setfield(m_state, LUA_REGISTRYINDEX, "batchsmith.env");
    // 弹掉副本后栈仍是 [env]

    set_phase("install_libraries");
    install_libraries();
    set_phase("register_helpers");
    helper::register_helpers(*this);
    set_phase("patch_functions");
    patch_unsafe_functions();
    set_phase("metatable");

    // env 的元表：__index 提供 i / rows，并记录是否用到行上下文
    lua_newtable(m_state);
    lua_pushcfunction(m_state, env_index_metamethod);
    lua_setfield(m_state, -2, "__index");
    lua_setmetatable(m_state, -2);

    lua_pop(m_state, 1);  // env（注册表里已有一份）
    set_phase("ready");
}

void Sandbox::inject_lists(const ListSourceList& sources) {
    set_phase("inject_lists");
    m_list_names.clear();
    m_list_items.clear();
    m_list_paddings.clear();
    m_list_items.reserve(sources.size());
    m_list_paddings.reserve(sources.size());

    lua_getfield(m_state, LUA_REGISTRYINDEX, "batchsmith.env");
    for (const ListSource& source : sources) {
        m_list_names.append(source.name);
        m_list_items.append(source.items);
        m_list_paddings.append(source.padding);

        const int list_index = static_cast<int>(m_list_items.size());

        lua_newtable(m_state);
        for (qsizetype i = 0; i < source.items.size(); ++i) {
            const QByteArray utf8 = source.items.at(i).toUtf8();
            lua_pushlstring(m_state, utf8.constData(), static_cast<size_t>(utf8.size()));
            lua_rawseti(m_state, -2, static_cast<lua_Integer>(i + 1));
        }

        // 元方法只捕获列表下标，值回查沙箱；不设 __len ⇒ #list 等于原始长度
        lua_newtable(m_state);
        lua_pushinteger(m_state, list_index);
        lua_pushcclosure(m_state, list_index_metamethod, 1);
        lua_setfield(m_state, -2, "__index");
        lua_setmetatable(m_state, -2);

        lua_setfield(m_state, -2, source.name.toUtf8().constData());
    }
    lua_pop(m_state, 1);  // env
}

void Sandbox::set_row_context(qsizetype row, qsizetype rows) {
    m_current_row = row;
    m_total_rows = rows;
}

}  // namespace batchsmith::core::sandbox
