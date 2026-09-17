#include "batchsmith/core/sandbox/sandbox.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(BATCHSMITH_ALLOC_AUDIT)
#include <unordered_map>
#endif

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

#if defined(BATCHSMITH_ALLOC_AUDIT)

/// 诊断用：给分配器发出去的每一块记账，专门查「分配器收到的入参是否自洽」。
///
/// **动机**（2026-09-17）：MSVC Release 下单元测试以 `0xC0000374`（堆损坏）失败，
/// 而同一套用例在 ASan（Release + 插桩）与 Debug 调试堆下**全部干净**。
/// 越界写在任何一种插桩下都会被抓到 —— "全绿"本身就说明它不是越界写，
/// 更可能是 **free / realloc 收到了未知指针，或大小与当初申请的不符**：
/// ASan 换掉了整个分配器，这类调用会被它"消化"成正常调用；而 NT 堆在 LFH 上
/// 会直接判定堆损坏、一句有用的话都不说。这把仪器就是要把这个猜测变成
/// 一条可读的 stderr —— 抓到就停机，抓不到也等于排除掉一大类原因。
///
/// 前提：沙箱不跨线程使用（单线程假设成立）。map 刻意用 `new` 泄漏出去，
/// 免得"静态对象析构顺序"这种与主题无关的坑混进来。
struct AllocAudit {
    std::unordered_map<const void*, std::size_t> live;
    std::size_t allocations = 0;
    std::size_t releases = 0;
};

AllocAudit& alloc_audit() {
    static AllocAudit* instance = new AllocAudit();  // 故意泄漏
    return *instance;
}

/// 违规停机码。
///
/// 选 **77** 是因为它能穿过 bash 的退出码翻译：bash 对无法映射到信号的 NTSTATUS
/// （包括我们要区分的 `0xC0000374`）一律报 **127**，与"命令找不到"撞车；
/// 77 则原样透出，CI 侧一眼可辨。
constexpr int kAllocAuditExitCode = 77;

[[noreturn]] void alloc_audit_fail(const char* what,
                                   const void* block,
                                   std::size_t called_with,
                                   std::size_t recorded) {
    const AllocAudit& audit = alloc_audit();
    std::fprintf(stderr,
                 "[batchsmith] 分配器审计失败：%s\n"
                 "  块=%p  记账大小=%zu  调用方传入 old_size=%zu\n"
                 "  累计：分配 %zu 次 / 释放 %zu 次 / 存活 %zu 块\n",
                 what,
                 block,
                 recorded,
                 called_with,
                 audit.allocations,
                 audit.releases,
                 audit.live.size());
    std::fflush(stderr);
    std::_Exit(kAllocAuditExitCode);
}

/// 分配/释放**之前**：查这次调用本身是否自洽。
void alloc_audit_before(void* block, std::size_t old_size, std::size_t new_size) {
    AllocAudit& audit = alloc_audit();
    if (block == nullptr) {
        return;  // 全新分配：没有可查的历史
    }
    const auto it = audit.live.find(block);
    if (it == audit.live.end()) {
        alloc_audit_fail(new_size == 0 ? "释放了一个不属于本分配器的指针"
                                       : "realloc 了一个不属于本分配器的指针",
                         block,
                         old_size,
                         0);
    }
    if (it->second != old_size) {
        alloc_audit_fail(
                "old_size 与当年申请的字节数不符（记账被写坏了）", block, old_size, it->second);
    }
}

/// 分配/释放**之后**：把账本更新成与真实状态一致。
void alloc_audit_after(void* block, std::size_t new_size, void* result) {
    AllocAudit& audit = alloc_audit();
    if (block == nullptr) {
        if (result != nullptr) {  // 分配失败（例如撞上内存上限）就不记账
            audit.live.emplace(result, new_size);
            ++audit.allocations;
        }
        return;
    }
    if (new_size == 0) {
        const auto it = audit.live.find(block);
        const std::size_t recorded = it == audit.live.end() ? 0 : it->second;
        if (result != nullptr) {
            alloc_audit_fail("释放的返回值不是 nullptr（分配器契约被破坏）", block, 0, recorded);
        }
        audit.live.erase(block);
        ++audit.releases;
        return;
    }
    // realloc：失败时调用方仍认为老块有效（Lua 的约定），所以只有成功才动账。
    if (result == nullptr) {
        return;
    }
    audit.live.erase(block);
    audit.live.emplace(result, new_size);
}

/// `lua_close` 结束后账本应该一块不剩。剩了就说明有块没被 Lua 释放。
/// 只打警告不判红 —— 这条是"顺手多看一眼"，不是本次要查的主线。
void alloc_audit_report_leaks(const char* where) {
    AllocAudit& audit = alloc_audit();
    if (audit.live.empty()) {
        return;
    }
    std::fprintf(stderr,
                 "[batchsmith] 分配器审计警告：%s 之后仍有 %zu 块未释放\n",
                 where,
                 audit.live.size());
    for (const auto& entry : audit.live) {
        std::fprintf(stderr, "  块=%p 大小=%zu\n", entry.first, entry.second);
    }
    std::fflush(stderr);
    audit.live.clear();  // 下一个沙箱从干净的账本开始
}

#endif  // BATCHSMITH_ALLOC_AUDIT

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

#if defined(BATCHSMITH_ALLOC_AUDIT)
/// 审计版分配器：只在调用前后各加一层"入参自洽性"检查，其余原样转发。
///
/// 之所以用**包一层**而不是往 `tracked_allocator` 的每个 return 点塞检查：
/// 那个函数有 4 个出口（Lua 创建窗口 / 关闭窗口 / 释放 / 内存上限拒绝），
/// 逐点插入一旦漏一处，得到的就是"看起来跑了、其实没查全"的假证据。
void* tracked_allocator_audited(void* ud, void* ptr, size_t old_size, size_t new_size) {
    alloc_audit_before(ptr, old_size, new_size);
    void* result = tracked_allocator(ud, ptr, old_size, new_size);
    alloc_audit_after(ptr, new_size, result);
    return result;
}

#define BATCHSMITH_LUA_ALLOCATOR tracked_allocator_audited
#else
#define BATCHSMITH_LUA_ALLOCATOR tracked_allocator
#endif

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

/// 把 `self` 写进 `state` 的 extraspace。
///
/// 用 `memcpy` 而不是 `*static_cast<Sandbox**>(...) = self`：extraspace 是
/// `char[sizeof(void*)]`，**它的对齐只保证到 1** —— 类型双关写入等于假设了目标
/// 8 字节对齐，那是未定义行为（换个编译器或优化档就可能被编成要求对齐的指令）。
/// 读侧同理（见 `load_self`）。memcpy 的这点开销编译器会直接优化掉。
void store_self(lua_State* state, Sandbox* self) {
    std::memcpy(lua_getextraspace(state), &self, sizeof(self));
}

[[nodiscard]] Sandbox* load_self(lua_State* state) {
    Sandbox* self = nullptr;
    std::memcpy(&self, lua_getextraspace(state), sizeof(self));
    return self;
}

}  // namespace

Sandbox::Sandbox(Limits limits) : m_limits(limits) {
    m_state = lua_newstate(BATCHSMITH_LUA_ALLOCATOR, nullptr);
    if (m_state == nullptr) {
        raise(Violation::Memory, QStringLiteral("无法创建 Lua 状态"));
        return;
    }

    // ⚠️ 顺序是硬要求：**先写 extraspace，再让分配器拿到账本**。
    //
    // 分配器要靠 extraspace 里的 `Sandbox*` 才找得到账本，而它一被挂上
    // （`lua_setallocf` 把 ud 设成 m_state）就会走那条路径。两行若调换，中间
    // 任何一次分配都会用到 `lua_newstate` 留下的**未初始化 extraspace** ——
    // 那是个随机指针，`sandbox->account()` 就会往野内存里记账。
    //
    // 这个窗口当前是"空的"（中间只有不分配的 `lua_atpanic`），但"靠中间没有分配
    // 来保证安全"太脆：加一行日志、换个 Lua 版本都可能打破它。顺序固定下来之后，
    // 无论中间发生什么都只读到正确指针。
    store_self(m_state, this);

    lua_setallocf(m_state, BATCHSMITH_LUA_ALLOCATOR, m_state);
    lua_atpanic(m_state, panic_handler);

    m_account.started = Clock::now();
    lua_sethook(m_state, limit_hook, LUA_MASKCOUNT, m_limits.instruction_slice);
    build_environment();
}

Sandbox::~Sandbox() {
    if (m_state != nullptr) {
        // 先清掉 extraspace，避免关闭过程中的分配触发我们的记账/报错
        store_self(m_state, nullptr);
        lua_close(m_state);
#if defined(BATCHSMITH_ALLOC_AUDIT)
        alloc_audit_report_leaks("lua_close");
#endif
    }
}

#undef BATCHSMITH_LUA_ALLOCATOR

Sandbox* Sandbox::from_state(lua_State* state) {
    return load_self(state);
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
