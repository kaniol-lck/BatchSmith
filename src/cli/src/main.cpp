#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "batchsmith/core/version.hpp"

namespace {

/// CLI 的输出统一走 stdout。
/// 不用 qDebug：Release 构建可能带 QT_NO_DEBUG_OUTPUT，输出会被编译期屏蔽，
/// 而 CLI 的输出是它的对外契约，不能被构建配置改掉。
///
/// 目前只有正常输出，所以不设 stderr 通道；等 M1 起有真正的错误路径时再加，
/// 避免留一个没人用的函数。
QTextStream& out_stream() {
    static QTextStream stream(stdout);
    return stream;
}

void print_plan() {
    QTextStream& out = out_stream();
    out << "\n"
        << "尚未实现的子命令（见 docs/技术方案与实现路线.md §7）：\n"
        << "  bs eval  <模板>              只跑 DSL 编译与沙箱求值，不碰文件系统（M1）\n"
        << "  bs plan  <预设> --bind k=v   生成 Plan 并打印 diff，不产生任何副作用（M2）\n"
        << "  bs run   <预设> --bind k=v   执行；默认 dry-run，需 --apply 才真正落盘（M2/M3）\n"
        << "  bs undo  <日志>              按撤销日志逆序回放（M3）\n"
        << "\n"
        << "当前只有 core 库的骨架（版本信息与自然排序比较器），命令尚未接入。\n";
    out.flush();
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(Q_OS_WIN)
    // Windows 控制台默认不是 UTF-8，不设这一行中文输出会乱码
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bs"));
    QCoreApplication::setApplicationVersion(
            QString::fromLatin1(batchsmith::core::version_string()));
    QCoreApplication::setOrganizationName(QStringLiteral("BatchSmith"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
            QStringLiteral("BatchSmith 命令行 —— 把列表与表达式编译成批量操作"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption presetOption({QStringLiteral("p"), QStringLiteral("preset")},
                                          QStringLiteral("要加载的预设文件（TOML）。"),
                                          QStringLiteral("文件"));
    const QCommandLineOption bindOption(
            {QStringLiteral("b"), QStringLiteral("bind")},
            QStringLiteral("绑定预设里的路径槽位，形如 input=D:/anime。可重复。"),
            QStringLiteral("槽位=值"));
    const QCommandLineOption applyOption(QStringLiteral("apply"),
                                         QStringLiteral("真正执行。不加此参数时一律 dry-run。"));
    parser.addOption(presetOption);
    parser.addOption(bindOption);
    parser.addOption(applyOption);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("子命令，见下方说明。"));

    parser.process(app);

    QTextStream& out = out_stream();
    out << QString::fromLatin1(batchsmith::core::version_banner()) << "\n";
    print_plan();

    (void)presetOption;
    (void)bindOption;
    (void)applyOption;

    // 功能未接入前返回非零，避免被脚本误当成"执行成功"。
    // M1 起每个子命令会有自己的退出码语义。
    return 2;
}
