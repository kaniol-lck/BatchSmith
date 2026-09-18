#pragma once

#include <chrono>
#include <random>

#include <QString>
#include <QStringList>

#include "batchsmith/core/list/list_source.hpp"

struct lua_State;

namespace batchsmith::core::sandbox {

/// 限制被触发的方式。宿主侧记录，脚本里 `pcall` 吞不掉（ADR-6 逃逸面 3）。
enum class Violation {
    None,
    Instructions,  ///< 指令数超上限
    Memory,        ///< 内存超上限
    WallClock,     ///< 墙钟超时
    Output,        ///< 展开行数超上限（宿主侧判定，不是 Lua 违规）
};

struct Limits {
    qsizetype max_memory_bytes = 64LL * 1024 * 1024;
    qsizetype max_instructions = 20'000'000;
    int instruction_slice = 10'000;  ///< 钩子触发间隔（越大开销越小、响应越粗）
    qsizetype max_wall_ms = 3000;

    /// 单次字符串构造的上限，用于 `string.rep` / `string.format` / `table.concat` 的
    /// **参数预检**（ADR-6 逃逸面 1：这些函数会先分配再检查，等内存限制生效就晚了）
    qsizetype max_string_bytes = 1LL * 1024 * 1024;

    /// 一次求值最多产出多少行（防止展开把界面/内存拖死）
    qsizetype max_rows = 100000;
};

/// 一个受限制的 Lua 运行时：白名单环境 + 四重限制。
///
/// **不调用 `luaL_openlibs`**：只手工打开 `string` / `table` / `math` / `utf8`，
/// 把结果表挂进自建的 env，再把 env 作为 chunk 的 `_ENV`。于是 `io` / `os` / `package` /
/// `require` / `load` / `dofile` / `loadfile` / `debug` / `collectgarbage` / `coroutine`
/// 全都读不到（读未知全局名会走 env 的 `__index`，返回 nil）。
///
/// 行上下文 `i` / `rows` 刻意**不放进 env 表**，而是由 `__index` 提供 —— 这样宿主能
/// 精确知道模板有没有用到行上下文（用于「模板不需要行上下文时只求值一次」的判定）。
class Sandbox {
public:
    explicit Sandbox(Limits limits = {});
    ~Sandbox();

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    [[nodiscard]] lua_State* state() const { return m_state; }

    [[nodiscard]] const Limits& limits() const { return m_limits; }

    [[nodiscard]] std::mt19937_64& rng() { return m_rng; }

    /// 由任意 Lua 状态取回它对应的沙箱（沙箱指针存在 `lua_getextraspace` 里）
    [[nodiscard]] static Sandbox* from_state(lua_State* state);

    /// 运行时账本：内存、指令数、起始时刻。
    ///
    /// ⚠️ 必须放在 Sandbox 本体里，**不能**塞进 `lua_getextraspace`：
    /// 那块区域默认只有 `sizeof(void*)`（8 字节），按 24 字节去写会踩坏堆内存 ——
    /// 症状是莫名其妙的卡死/崩溃，而不是报错。extraspace 只放 Sandbox* 一个指针。
    struct Account {
        qsizetype bytes = 0;
        qsizetype instructions = 0;
        std::chrono::steady_clock::time_point started{};
    };

    [[nodiscard]] Account& account() { return m_account; }

    [[nodiscard]] const Account& account() const { return m_account; }

    [[nodiscard]] qsizetype elapsed_ms() const;

    /// 当前所处阶段，仅供崩溃诊断：
    /// 未保护的 Lua 错误会让 Lua 直接 abort()，栈上什么线索都不留；
    /// panic 处理器把这个标记打出来，就能一眼看出崩在装配的哪一步。
    void set_phase(const char* phase) { m_phase = phase; }

    [[nodiscard]] const char* phase() const { return m_phase; }

    /// 把输入列表注入 env：普通表 + `__index` 元方法（ADR-8）。
    ///
    /// 值真实存在于 `1..n`，**不设 `__len`** ⇒ `#list` 天然等于原始长度；
    /// 越界读取走元方法 ⇒ 按该列表的缺省方式返回（默认空串），**不返回 nil**。
    void inject_lists(const ListSourceList& sources);

    /// 设置当前行上下文；模板读取 `i` / `rows` 会被记录为「用到了行上下文」
    void set_row_context(qsizetype row, qsizetype rows);

    [[nodiscard]] qsizetype current_row() const { return m_current_row; }

    [[nodiscard]] qsizetype total_rows() const { return m_total_rows; }

    [[nodiscard]] bool row_context_used() const { return m_row_context_used; }

    void clear_row_context_used() { m_row_context_used = false; }

    void mark_row_context_used() { m_row_context_used = true; }

    /// 记一次违规。**宿主侧独立标记** —— 脚本里 `pcall` 捕获到中止错误也照样记上。
    void raise(Violation kind, const QString& message);

    [[nodiscard]] Violation violation() const { return m_violation; }

    [[nodiscard]] QString violation_message() const { return m_violation_message; }

    /// 诊断用：把任意一个 `QString` 的**数据块页面性质 + 头部字段 + 堆归属**打到 stderr。
    ///
    /// 为什么需要它（run #32 的现场）：崩溃点是 Qt 的 `QString` 拷贝构造里对源对象
    /// 的 `d` 做引用计数自增（`lock xadd`），源对象正是 `m_violation_message`，
    /// 而它的三件套**完全自洽**（`ptr == d + 16`、`size == 17`，正好等于
    /// 「指令数超过上限（20000000）」的长度）—— 只有 `d` 所在的页是
    /// **已保留但未提交**，于是那句 `lock xadd` 当场 AV。
    /// 也就是说：不是因为对象被踩坏，而是**它指向的内存已经不属于我们了**。
    ///
    /// run #33 进一步把区间缩到了「`raise()` 之内是好的、`raise()` 返回之后就是坏的」，
    /// 但同时暴露了一个自相矛盾：同一行里既报「已保留」又成功读出了长度。
    /// 本机实测（`.workbuddy/tools/pageprobe/`）确认「已保留 = 读写都当场 AV」，
    /// 所以那个"长度"不可能真是读出来的 —— 于是这个探针改成：
    ///   ① **先判可读、再读**，读不动就明说「读不动」，绝不越过这一步；
    ///   ② 长度取自**头部字节**（`ref/size/flags`），不再经 `QString::size()`；
    ///   ③ 加 `HeapSize` 交叉验证（遍历所有堆），它和 `VirtualQuery` 是两条独立的证据链。
    ///
    /// ⚠️ 只在诊断构建里有实现（`BATCHSMITH_MESSAGE_TRACE`），且还要环境变量
    /// `BATCHSMITH_MESSAGE_TRACE=1` 才真的打印 —— 否则同一次构建里的单元测试
    /// 会被这条探针刷屏。非诊断构建下是空调用，零开销。
    void trace_message(const char* where, const char* label, const QString& value) const;

    /// 同上，专门看 `m_violation_message`。
    void trace_violation_message(const char* where) const;

    /// 诊断用：把 `m_violation_message` 的数据块**所在的整页改成只读** ⇒ 之后任何写它的指令
    /// 都会当场 AV，被沙箱装的 VEH 拦下并打印「哪个模块 + RVA（+ 寄存器）」。
    ///
    /// 用途（run #37 的结论）：在 `raise()` 里多持一份引用之后，中止分支不再崩、但块**照样**
    /// 被释放，崩点挪到了退出时的 `~QString()` ⇒ 说明块的**引用计数被多减了一次**，而且那一减
    /// 发生在 `raise()` 返回之后、`lua_pcall` 返回之前（探针 ref 2→1）。那一段里没有一行 Qt
    /// 代码，日志切不动了，只能让那次写自己撞墙。
    ///
    /// 需要环境变量 `BATCHSMITH_MESSAGE_PAGE_WATCH=1`
    /// 才会动手；`BATCHSMITH_MESSAGE_PAGE_WATCH_SELFTEST=1`
    /// 时先在一块自造的内存上跑一遍自检（证明仪器真的能拦下写，而不是"没报=没事"）。
    void arm_message_write_watch() const;

    /// 摘掉写看门狗（把页恢复成可写）。必须在块可能被**堆释放**之前调用 —— 否则堆写自己的块头
    /// 也会撞上只读页，那就是仪器自己制造故障了。
    void disarm_message_write_watch() const;

    void clear_violation();

    // ---- 供 helper 内部使用 ----
    [[nodiscard]] qsizetype list_count() const { return m_list_items.size(); }

    [[nodiscard]] const QStringList& list_names() const { return m_list_names; }

    [[nodiscard]] const QStringList& list_items(qsizetype index) const {
        return m_list_items.at(index);
    }

    [[nodiscard]] ListPadding list_padding(qsizetype index) const {
        return m_list_paddings.at(index);
    }

private:
    void build_environment();
    void install_libraries();
    void patch_unsafe_functions();

    lua_State* m_state = nullptr;
    Limits m_limits;

    // 违规记录（ADR-6 逃逸面 3）
    Violation m_violation = Violation::None;
    QString m_violation_message;

    // 行上下文
    qsizetype m_current_row = 1;
    qsizetype m_total_rows = 1;
    bool m_row_context_used = false;

    // 列表的值与缺省方式（Lua 侧只放存根，元方法按索引回查这里，避免重复拷贝）
    QStringList m_list_names;
    QList<QStringList> m_list_items;
    QList<ListPadding> m_list_paddings;

    std::mt19937_64 m_rng{0xB47C45'11ULL};  // 固定种子：dry-run 与实际执行必须一致

    Account m_account;
    const char* m_phase = "ctor";
};

}  // namespace batchsmith::core::sandbox
