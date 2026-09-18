// ===========================================================================
// 崩溃现场自报（**仅诊断构建**，默认关闭；见 CMakeLists 的 BATCHSMITH_CRASH_REPORT）
//
// 为什么需要它 —— 2026-09-18 的实测把三条路全堵死了：
//
//   ① **WER 在 GitHub runner 上根本不产事件**：run #27 / #28 的
//      `wer-application-error.xml` 与 `wer-wer.xml` 都是 **0 字节**，LocalDumps 配好了
//      也没有迷你转储（它依赖 WER）⇒ "让 AV 变成未处理异常、由 WER 记 Fault offset"
//      这条思路在 CI 上不成立。
//   ② 输出文件也是 0 字节：`QTextStream` 有内部缓冲，进程被异常终结时**没人替它 flush**
//      ⇒ 连"跑到哪儿了"都读不到。
//   ③ 但 PageHeap 整跑给了一条**反证**：`[doctest] test cases: 57 | 56 passed | 1 failed`
//      —— 测试二进制里那个 AV **被 doctest 的未处理异常过滤器接住了**。
//
//   结论：这个 AV 是**能**被进程内处理器拿到的，只要别指望 WER。
//
// 做法：装一个 **vectored exception handler**（VEH 比 SEH / UnhandledExceptionFilter
// 更早被调用，`AddVectoredExceptionHandler` 的第一个参数非 0 表示"插到最前面"），
// 在里头把出错位置折成"模块名 + RVA"、再走一遍调用链，然后立刻退出。
//
// run #28（`39e321b`）第一次跑通了这个仪器，也第一次当场暴露了它自己的短板：
// **出错指令落在别的模块里**（`bs.exe` 之外），而当时的实现只对主模块算 RVA
// ⇒ 那条最有价值的信息只打出一个 `0`。同一轮还纠正了一条旧口径：PageHeap 对 `bs.exe`
// 是**开着的**（IFEO 的 `GlobalFlag=0x02000000`），而 bug 在全页堆下照样复现
// ⇒ **"只在 LFH 布局下致命"这个说法不成立**；全页堆是页粒度 + 守卫页的另一套分配器，
// 它做的恰恰是把"无声的堆破坏"变成"当场 AV"。
//
// 所以现在一次报全四样：
//   * **出错指令**所在模块的**名字**与 RVA（不管那是主模块还是别人的）；
//   * 异常码 / 访问类型（读/写/执行）/ 访问地址；
//   * **调用链** —— 这才是能落到我们自己代码上的那条线索。用 PE 的 unwind 信息
//     （`.pdata`）步行，等价于 `StackWalk64` 的核心，**不需要 PDB、不需要 dbghelp**；
//   * 主模块的名字与基址（用于确认"到底是不是同一个模块"）。
//
// ⚠️ 四条硬约束，全因为"此刻堆已经不可信"：
//   * **不分配内存**：只用栈上的固定缓冲与 `snprintf`；模块名缓存用静态数组。
//   * **不走 atexit / 不 flush 别的流**：退出用 `_Exit`，别让退出钩子再崩一次。
//   * **不假设异常发生在主模块里**（run #28 的教训）。
//   * NTDLL 的两个入口用 `GetProcAddress` 取，不靠链接期符号 —— MSVC 与 MinGW 的
//     默认链接库都**不含 ntdll**（`winnt.h` 里它们被声明成 `dllimport`），
//     直接调用会在**链接**步炸掉，而这是诊断构建，不想为它多引一个库。
//
// 自检：诊断构建里带一段**已知深度**的递归 + 空指针写，只要
// `BATCHSMITH_CRASH_SELFTEST=1 bs --version`，就必须打出自报（含调用链）且退出码 42。
// 为什么非要自检：这个项目在"仪器其实没生效"上白扔过好几轮，而**没有自检的仪器
// 在崩之前看起来和"装好了"完全一样**。
// ===========================================================================

#if defined(BATCHSMITH_CRASH_REPORT) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

/// 自报完就退出的退出码。
///
/// 选 **42** 而不是让异常继续往上走的原因：继续走就会落到 UnhandledExceptionFilter，
/// 而在 runner 上那条路**什么都不会留下**（①）。42 也不会与别的退出码撞车：
/// 0/1/2/3 是 CLI 自己的语义，127/139 是 bash 对 NTSTATUS 的糊化，77 是分配器审计。
constexpr int kCrashReportExitCode = 42;

/// 调用链最多报这么多帧。够看到"系统 DLL → 我们的代码"那一步过渡即可；
/// 再多也只是把 ntdll 的派发帧也列进来。
constexpr int kMaxFrames = 24;

/// 模块名缓存的容量（静态存储 —— 这里不能分配）。真撞满了就退化成打印基址。
constexpr int kMaxModules = 24;

struct module_slot {
    std::uintptr_t base;
    char name[80];
};

module_slot g_modules[kMaxModules];
int g_module_count = 0;

/// 步行调用链期间再崩一次 ⇒ 是步行本身踩了（unwind 数据不可信），别再递归进来。
bool g_walking = false;

void emit_line(const char* text) {
    std::fwrite(text, 1, std::strlen(text), stderr);
    std::fflush(stderr);  // 每行都落地：下一帧可能就崩了
}

/// 取地址所在**映像**的基址。对映像映射来说 `AllocationBase` 就是模块基址。
///
/// 不用 `GetModuleHandleEx`：它按名字查，而这里手上只有一个地址。
std::uintptr_t image_base_of(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    std::memset(&mbi, 0, sizeof(mbi));
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0) {
        return 0;
    }
    if (mbi.Type != MEM_IMAGE) {
        return 0;  // 堆 / 栈 / 私有映射：没有"模块"可言
    }
    return reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
}

/// 模块基址 → 文件名（缓存）。拿不到就退化成十六进制基址。
const char* module_name_of(std::uintptr_t base) {
    if (base == 0) {
        return "?";
    }
    for (int i = 0; i < g_module_count; ++i) {
        if (g_modules[i].base == base) {
            return g_modules[i].name;
        }
    }
    if (g_module_count >= kMaxModules) {
        return "?";  // 缓存满了就直说，别为它去分配
    }
    module_slot& slot = g_modules[g_module_count];
    ++g_module_count;
    slot.base = base;
    char path[MAX_PATH];
    path[0] = '\0';
    const DWORD written = ::GetModuleFileNameA(reinterpret_cast<HMODULE>(base), path, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) {
        std::snprintf(
                slot.name, sizeof(slot.name), "0x%llx", static_cast<unsigned long long>(base));
        return slot.name;
    }
    const char* slash = std::strrchr(path, '\\');
    std::snprintf(slot.name, sizeof(slot.name), "%s", slash != nullptr ? slash + 1 : path);
    return slot.name;
}

/// 把地址写成 `模块名 + 0xRVA`。写进调用方给的缓冲，不分配。
void describe_address(std::uintptr_t address, char* out, std::size_t out_size) {
    const std::uintptr_t base = image_base_of(address);
    if (base == 0) {
        std::snprintf(out,
                      out_size,
                      "0x%llx（不在任何映像里）",
                      static_cast<unsigned long long>(address));
        return;
    }
    std::snprintf(out,
                  out_size,
                  "%s + 0x%llx",
                  module_name_of(base),
                  static_cast<unsigned long long>(address - base));
}

/// 只报"真·故障"，别把 C++ 异常、调试事件这些正常的异常流也拦下来。
bool is_fault(std::uint32_t code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case 0xC0000374u:  // STATUS_HEAP_CORRUPTION（堆管理器自己报的那一个）
            return true;
        default:
            return false;
    }
}

const char* access_kind(const EXCEPTION_RECORD* record) {
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return "";
    }
    if (record->NumberParameters < 2) {
        return "（访问违例，但没带参数）";
    }
    const ULONG_PTR how = record->ExceptionInformation[0];
    const ULONG_PTR where = record->ExceptionInformation[1];
    // 只填静态缓冲：这里不能分配
    static char buf[96];
    std::snprintf(buf,
                  sizeof(buf),
                  "%s 地址 %#llx",
                  how == 0 ? "**读**" : (how == 1 ? "**写**" : (how == 8 ? "**执行**" : "?")),
                  static_cast<unsigned long long>(where));
    return buf;
}

// NTDLL 里那两个步行入口。用函数指针取，免掉"默认链接库里没有 ntdll"这件事。
using rtl_lookup_fn = PRUNTIME_FUNCTION(NTAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);
using rtl_unwind_fn = void*(NTAPI*)(DWORD,
                                    DWORD64,
                                    DWORD64,
                                    PRUNTIME_FUNCTION,
                                    PCONTEXT,
                                    PVOID*,
                                    PDWORD64,
                                    PKNONVOLATILE_CONTEXT_POINTERS);

struct walker {
    rtl_lookup_fn lookup = nullptr;
    rtl_unwind_fn unwind = nullptr;
};

walker resolve_walker() {
    walker result;
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return result;
    }
    result.lookup = reinterpret_cast<rtl_lookup_fn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlLookupFunctionEntry")));
    result.unwind = reinterpret_cast<rtl_unwind_fn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlVirtualUnwind")));
    return result;
}

/// 这个地址是否可读（已提交、且不是守卫页 / 不可访问页）。步行时读栈顶要用。
bool readable(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    std::memset(&mbi, 0, sizeof(mbi));
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    return (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
}

/// 用 PE 的 unwind 信息把调用链走一遍，从**出错函数自己**（#0）开始。
///
/// 为什么不用 `RtlCaptureStackBackTrace`：它是从**当前** RSP 往上数的，而我们此刻
/// 已经在异常派发器新建的帧里，数出来的头几帧是 ntdll 的派发代码，真正出错的那条链
/// 在**更高**的地址上、接不上。`RtlVirtualUnwind` 走的是 `ContextRecord` 里的 RSP/RIP，
/// 所以起点就是出错现场本身。
void walk_stack(const CONTEXT* fault_context) {
    const walker tools = resolve_walker();
    if (tools.lookup == nullptr || tools.unwind == nullptr) {
        emit_line("    （拿不到 RtlLookupFunctionEntry / RtlVirtualUnwind，只能停在第一帧）\n");
    }

    CONTEXT ctx = *fault_context;
    char line[224];
    for (int i = 0; i < kMaxFrames; ++i) {
        char desc[160];
        describe_address(static_cast<std::uintptr_t>(ctx.Rip), desc, sizeof(desc));
        std::snprintf(line, sizeof(line), "    #%-2d %s\n", i, desc);
        emit_line(line);

        if (tools.lookup == nullptr || tools.unwind == nullptr) {
            return;
        }

        const std::uintptr_t prev_rsp = static_cast<std::uintptr_t>(ctx.Rsp);
        DWORD64 image_base = 0;
        const PRUNTIME_FUNCTION entry = tools.lookup(ctx.Rip, &image_base, nullptr);
        if (entry != nullptr) {
            PVOID handler_data = nullptr;
            DWORD64 establisher = 0;
            tools.unwind(UNW_FLAG_NHANDLER,
                         image_base,
                         ctx.Rip,
                         entry,
                         &ctx,
                         &handler_data,
                         &establisher,
                         nullptr);
        } else {
            // 叶子函数没有 unwind 信息：返回地址就在栈顶。
            if (!readable(prev_rsp)) {
                emit_line("    （栈顶不可读，步行就此打住）\n");
                return;
            }
            ctx.Rip = *reinterpret_cast<const DWORD64*>(prev_rsp);
            ctx.Rsp = prev_rsp + sizeof(DWORD64);
        }

        // 两道防跑飞：地址归零，或栈指针没往高处走（栈向下长，回溯必然递增）。
        if (ctx.Rip == 0 || static_cast<std::uintptr_t>(ctx.Rsp) <= prev_rsp) {
            return;
        }
    }
    emit_line("    （到帧数上限了）\n");
}

LONG WINAPI crash_reporter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info != nullptr ? info->ExceptionRecord : nullptr;
    if (record == nullptr || !is_fault(record->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;  // 不是故障：让别的处理器接着看
    }

    // 步行途中又崩：那就是 unwind 数据本身不可信，别再递归进来。
    if (g_walking) {
        emit_line("\n[batchsmith] 崩溃自报：走调用链时又崩了一次（unwind 数据不可信）\n");
        std::_Exit(kCrashReportExitCode);
    }

    const auto address = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    // 主模块基址 = exe 的加载地址。GetModuleHandleW(nullptr) 走的是 PEB，不分配。
    const auto main_base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
    const std::uintptr_t fault_base = image_base_of(address);

    char line[320];
    std::snprintf(line,
                  sizeof(line),
                  "\n[batchsmith] 崩溃自报（VEH）：异常码 %#08lx  %s\n",
                  static_cast<unsigned long>(record->ExceptionCode),
                  access_kind(record));
    emit_line(line);

    char desc[160];
    describe_address(address, desc, sizeof(desc));
    std::snprintf(line,
                  sizeof(line),
                  "  出错指令 : %#llx\n"
                  "  所在模块 : %s%s\n",
                  static_cast<unsigned long long>(address),
                  desc,
                  (fault_base != 0 && fault_base == main_base) ? "（主模块）" : "（**外部模块**）");
    emit_line(line);

    std::snprintf(line,
                  sizeof(line),
                  "  主模块   : %s 基址 %#llx\n"
                  "  调用链（#0 = 出错函数自己，按 PE unwind 信息步行，最多 %d 帧）：\n",
                  module_name_of(main_base),
                  static_cast<unsigned long long>(main_base),
                  kMaxFrames);
    emit_line(line);

    if (info->ContextRecord != nullptr) {
        g_walking = true;
        walk_stack(info->ContextRecord);
        g_walking = false;
    }

    // 不走 atexit、不 flush 其它流：堆已经不可信了。
    // `std::_Exit` 是 `[[noreturn]]`，所以这里不写 `return` —— 写了会被判成不可达代码。
    std::_Exit(kCrashReportExitCode);
}

/// 自检用：故意造一条**已知深度**的调用链，再解引用空指针逼出一个真异常。
///
/// 为什么要递归而不是原地写空指针：原地写只能验证"VEH 装上了、RVA 算对了"，
/// 而**走调用链**这件事最需要被验证（它依赖 unwind 信息、依赖栈）；给一条已知深度的链，
/// 输出里就该出现同样数量的帧，一眼能看出步行是不是真的在工作。
///
/// `acc` 用 `volatile` 有两重作用：防住尾调用优化（否则整条链会被压成一帧，
/// 自检就白做了），以及真让深度参与运算（不然编译器有权把调用删掉）。
int selftest_crash(int depth) {
    volatile int acc = depth;
    if (depth > 0) {
        // 读-改-写分开写：C++20 起对 volatile 的复合赋值是弃用的（`-Werror=volatile`）。
        const int inner = selftest_crash(depth - 1);  // 结果要参与加法 ⇒ 不是尾调用
        acc = acc + inner;
        return acc;
    }
    volatile int* boom = nullptr;
    *boom = -1;
    return acc;
}

}  // namespace

/// 由 `main()` 在**最开头**调用（越早越好：要在任何 Sandbox 建立之前就装上）。
void install_crash_reporter() {
    ::AddVectoredExceptionHandler(1, crash_reporter);

    // 自检的开关。CI 里跑 `BATCHSMITH_CRASH_SELFTEST=1 bs --version` 来验收：
    // 必须打出自报（含调用链）且退出码为 42。
    if (const char* on = std::getenv("BATCHSMITH_CRASH_SELFTEST"); on != nullptr && on[0] == '1') {
        (void)selftest_crash(5);
    }
}

#endif  // BATCHSMITH_CRASH_REPORT && _WIN32
