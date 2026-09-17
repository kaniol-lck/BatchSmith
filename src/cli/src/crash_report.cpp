// ===========================================================================
// 崩溃现场自报（**仅诊断构建**，默认关闭；见 CMakeLists 的 BATCHSMITH_CRASH_REPORT）
//
// 为什么需要它 —— 2026-09-18 run #27 的实测把三条路全堵死了：
//
//   ① **WER 在 GitHub runner 上根本不产事件**：`wer-application-error.xml` 与
//      `wer-wer.xml` 都是 **0 字节**，LocalDumps 配好了也没有迷你转储（它依赖 WER）。
//      ⇒ "让 AV 变成未处理异常、由 WER 记 Fault offset"这条思路在 CI 上不成立。
//   ② 输出文件也是 0 字节：`QTextStream` 有内部缓冲，进程被异常终结时**没人替它 flush**
//      ⇒ 连"跑到哪儿了"都读不到。
//   ③ 但同一轮的 PageHeap 整跑给了一条**反证**：
//      `[doctest] test cases: 57 | 56 passed | 1 failed | 98 skipped`
//      —— 测试二进制里那个 AV **被 doctest 的未处理异常过滤器接住了**。
//
//   结论：这个 AV 是**能**被进程内处理器拿到的，只要别指望 WER。
//
// 做法：装一个 **vectored exception handler**（VEH 比 SEH / UnhandledExceptionFilter
// 更早被调用，`AddVectoredExceptionHandler` 的第一个参数非 0 表示"插到最前面"），
// 在里头把**出错指令地址**折算成"相对主模块基址的 RVA"，然后立刻退出。
// 有了 RVA + 链接器 `/MAP` 产出的 `bs.map`，不用调试器、不用转储、不用符号服务器，
// 就能把函数名翻出来（见 `.workbuddy/tools/ciwatch/symbolicate.py`）。
//
// ⚠️ 三条硬约束，全是因为"此刻堆已经不可信"：
//   * **不分配内存**：只用栈上的固定缓冲与 `snprintf`。这时候再 malloc 等于二次破坏现场。
//   * **不走 atexit / 不 flush 别的流**：退出用 `_Exit`，别让退出钩子再崩一次。
//   * **不假设异常发生在主模块里**：崩在 `Qt6Core.dll` 里时，RVA 就不该拿 `bs.map` 去翻，
//     所以模块基址与"是否主模块"必须一起打出来。
// ===========================================================================

#if defined(BATCHSMITH_CRASH_REPORT) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

LONG WINAPI crash_reporter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info != nullptr ? info->ExceptionRecord : nullptr;
    if (record == nullptr || !is_fault(record->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;  // 不是故障：让别的处理器接着看
    }

    const auto address = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    // 主模块基址 = exe 的加载地址。GetModuleHandleW(nullptr) 走的是 PEB，不分配。
    const auto main_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    // 用 VirtualQuery 判断出错地址落在哪张映像里（同样不分配）。
    // AllocationBase 对映像映射就是该模块的基址。
    std::uintptr_t block_base = 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != 0) {
        block_base = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
    }
    const bool in_main = (block_base != 0 && block_base == main_base);

    char line[512];
    const int n = std::snprintf(
            line,
            sizeof(line),
            "\n[batchsmith] 崩溃自报（VEH）：异常码 %#08lx  %s\n"
            "  出错指令 : %#llx   %s\n"
            "  → RVA    : %#llx   ← 拿这个查 %s 的 .map（RVA，与 Preferred load address 同口径）\n"
            "  堆/映像  : 基址 %#llx（%s）\n",
            static_cast<unsigned long>(record->ExceptionCode),
            access_kind(record),
            static_cast<unsigned long long>(address),
            in_main ? "在主模块里" : "在**别的模块**里（RVA 不与本文档同坐标系，别拿 bs.map 去翻）",
            static_cast<unsigned long long>(in_main && main_base != 0 ? address - main_base : 0),
            in_main ? "bs.exe" : "(非主模块)",
            static_cast<unsigned long long>(block_base),
            in_main ? "主模块" : "外部模块");
    if (n > 0) {
        const int len = n < static_cast<int>(sizeof(line)) ? n : static_cast<int>(sizeof(line)) - 1;
        std::fwrite(line, 1, static_cast<std::size_t>(len), stderr);
        std::fflush(stderr);
    }
    // 不走 atexit、不 flush 其它流：堆已经不可信了。
    // `std::_Exit` 是 `[[noreturn]]`，所以这里不写 `return` —— 写了会被判成不可达代码。
    std::_Exit(kCrashReportExitCode);
}

}  // namespace

/// 由 `main()` 在**最开头**调用（越早越好：要在任何 Sandbox 建立之前就装上）。
void install_crash_reporter() {
    ::AddVectoredExceptionHandler(1, crash_reporter);

    // 自检：故意解引用空指针，逼出一个真异常，验证"装了、算对了"。
    //
    // 为什么值得往诊断构建里写这么一行：这个项目在"仪器其实没生效"上白扔过好几轮
    // （采集步静默死、审计开关没打开、PageHeap 记完没重开……），而**没有自检的仪器
    // 在崩之前看起来和"装好了"完全一样**。有了它，CI 里一句
    // `BATCHSMITH_CRASH_SELFTEST=1 bs --version` 就能验收：必须打出 VEH 那行、
    // 且退出码为 42。
    //
    // 用 `volatile` 是为了别被优化掉。`-1` 而不是 `1` 无所谓，只是为了让它不像
    // "真在写什么有用的东西"。
    if (const char* on = std::getenv("BATCHSMITH_CRASH_SELFTEST"); on != nullptr && on[0] == '1') {
        volatile int* boom = nullptr;
        *boom = -1;
    }
}

#endif  // BATCHSMITH_CRASH_REPORT && _WIN32
