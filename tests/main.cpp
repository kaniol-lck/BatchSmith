// core 测试的入口。
//
// 用 DOCTEST_CONFIG_IMPLEMENT **而不是** ..._WITH_MAIN，为的是自己写 main()：
// 里面要做一件对"崩溃诊断"很关键的事 —— 关掉 stdout/stderr 的缓冲。
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

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

#if defined(_MSC_VER)
    // MSVC 的 assert 失败默认会弹一个对话框 —— 在 CI 上没人点它，job 就卡死在那儿了。
    // 改成只往 stderr 写，然后照常 abort。
    //
    // 这条为什么需要：我们给 Lua 开了 LUA_USE_APICHECK（见 third_party/CMakeLists.txt），
    // 它的断言正是用标准 assert 实现的；一旦 C API 的栈索引越界，我们希望**看到
    // 那一行断言**（文件与行号就是根因），而不是让 CI 挂住。
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    return doctest::Context(argc, argv).run();
}
