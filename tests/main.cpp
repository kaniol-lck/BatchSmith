// core 测试的入口。
//
// 用 DOCTEST_CONFIG_IMPLEMENT **而不是** ..._WITH_MAIN，为的是自己写 main()：
// 里面要做两件对"崩溃诊断"很关键的事 —— 关掉 stdout/stderr 的缓冲，以及
// 在 Windows/MSVC 的 Debug 构建里打开 CRT 调试堆的校验。
//
// 为什么：测试若以崩溃收场（堆损坏、空指针写…），缓冲区里还没落盘的内容会连
// 进程一起消失，而"崩溃前最后跑到的那个用例名"往往是唯一的定位线索。
// 我们就在 Windows/MSVC 的 CI 上遇过一次 `Exit code 0xc0000374`（堆损坏）：
// ctest 只留下一句异常退出码，连跑到哪儿了都看不出来。
#define DOCTEST_CONFIG_IMPLEMENT

#include <doctest/doctest.h>

#include <cstdio>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(_WIN32)

/// 当前进程的堆是否还自洽。
///
/// - MSVC 的 Debug 构建：走 CRT 调试堆的校验。它有**守卫字节**（每块分配前后各一段），
///   越界写会被当场指出来 —— 这是最强的检查，但也只有 Debug 构建才有。
/// - 其余 Windows 构建（含 MinGW 复现本问题时用的那些）：走 `HeapValidate`，
///   它只能查出堆的**结构**已经被破坏（块头/链表坏了），查不出"写到了别人的空闲
///   空间里"这种还没致伤的越界。聊胜于无：真出问题时这已经足够把范围缩小。
[[nodiscard]] bool heap_is_consistent() {
#if defined(_MSC_VER) && defined(_DEBUG)
    return _CrtCheckMemory() != FALSE;
#else
    return HeapValidate(GetProcessHeap(), 0, nullptr) != FALSE;
#endif
}

/// 每个用例结束时校验一次堆。
///
/// 为什么需要：**越界写的"发生"与"被发现"往往隔着很远**。越界会破坏堆块的守卫字节
/// 或块头，但直到那块内存被释放才会被 OS 发现（表现就是那个 0xc0000374），
/// 而那时崩溃点已经是**被害者**，不是元凶 —— 只看崩溃点会一直追错方向。
///
/// 每个用例之后主动校验，就能把"第一个让堆变得不一致的用例"直接指出来。
///
/// 校验失败时**只报告、不中断**：跑完一遍能一次拿到多个可疑用例，
/// 比"第一个就停"信息量大。
class HeapCheckListener : public doctest::IReporter {
public:
    explicit HeapCheckListener(const doctest::ContextOptions&) {}

    void report_query(const doctest::QueryData&) override {}

    void test_run_start() override {}

    void test_run_end(const doctest::TestRunStats&) override {}

    void test_case_reenter(const doctest::TestCaseData&) override {}

    void test_case_exception(const doctest::TestCaseException&) override {}

    void subcase_start(const doctest::SubcaseSignature&) override {}

    void subcase_end() override {}

    void log_assert(const doctest::AssertData&) override {}

    void log_message(const doctest::MessageData&) override {}

    void test_case_skipped(const doctest::TestCaseData&) override {}

    void test_case_start(const doctest::TestCaseData& test_case) override {
        m_current = test_case.m_name;
    }

    void test_case_end(const doctest::CurrentTestCaseStats&) override {
        if (heap_is_consistent()) {
            return;
        }
        std::fprintf(stderr, "[heap] 堆检查失败：跑完用例「%s」之后堆已经不一致\n", m_current);
        std::fflush(stderr);
    }

private:
    const char* m_current = "";
};

// 注册成 listener（第三个参数 false）—— 始终生效，不需要额外的命令行开关
DOCTEST_REGISTER_LISTENER("heap-check", 1, HeapCheckListener);

#endif  // _WIN32

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

#if defined(_MSC_VER)
    // MSVC 的 assert 失败与 CRT 报告默认会弹一个对话框 —— 在 CI 上没人点它，
    // job 就卡死在那儿了。全部改成只往 stderr 写。
    //
    // assert 那条为什么需要：我们给 Lua 开了 LUA_USE_APICHECK
    // （见 third_party/CMakeLists.txt），它的断言正是用标准 assert 实现的；
    // 一旦 C API 的栈索引越界，我们希望**看到那一行断言**（文件与行号就是根因），
    // 而不是让 CI 挂住。
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#if defined(_DEBUG)
    // 打开分配记录与退出时的全堆校验：进程结束前会再查一遍，
    // 于是"最后一个用例把堆弄坏了"也会在退出时报出来。
    int flags = _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    // CHECK_ALWAYS 会在**每次**分配/释放时校验整个堆 —— 慢得离谱，但能把出错的
    // 那一次操作直接抓出来。放进环境变量，需要在 CI 上临时打开时不必改代码。
    if (std::getenv("BATCHSMITH_CRT_CHECK_ALWAYS") != nullptr) {
        flags |= _CRTDBG_CHECK_ALWAYS_DF;
    }
    _CrtSetDbgFlag(flags);
#endif
#endif

    return doctest::Context(argc, argv).run();
}
