#if defined(Q_OS_WIN) || defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "batchsmith/core/dsl/cheatsheet.hpp"
#include "batchsmith/core/dsl/compiler.hpp"
#include "batchsmith/core/dsl/engine.hpp"
#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/version.hpp"

namespace {

/// CLI 的输出统一走 stdout / stderr。
/// 不用 qDebug：Release 构建可能带 QT_NO_DEBUG_OUTPUT，输出会被编译期屏蔽，
/// 而 CLI 的输出是它的对外契约，不能被构建配置改掉。
QTextStream& out_stream() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err_stream() {
    static QTextStream stream(stderr);
    return stream;
}

constexpr int kExitOk = 0;
constexpr int kExitEvalError = 1;
constexpr int kExitUsage = 2;

void print_plan() {
    QTextStream& out = out_stream();
    out << "\n"
        << "可用子命令：\n"
        << "  bs eval <模板> [--list 名=值1,值2]… [--ignore 名]… [--show-lua]\n"
        << "        只跑 DSL 编译与沙箱求值，**不接触文件系统**。输出为逐行结果。\n"
        << "  bs cheatsheet [--html | --hhc]\n"
        << "        打印 DSL 语法、工具函数与示例（与界面的「帮助」同一份内容）；\n"
        << "        --html 输出可独立打开的 HTML 手册；\n"
        << "        --hhc  输出 CHM 的目录文件（配 packaging/make-chm.sh 打 .chm）。\n"
        << "\n"
        << "尚未实现（见 docs/技术方案与实现路线.md §7）：\n"
        << "  bs plan  <预设> --bind k=v   生成 Plan 并打印 diff，不产生任何副作用（Phase 3）\n"
        << "  bs run   <预设> --bind k=v   执行；默认 dry-run，需 --apply 才真正落盘（Phase 4）\n"
        << "  bs undo  <日志>              按撤销日志逆序回放（Phase 4）\n";
}

/// `--list 名=值1,值2` → 列表源。逗号分隔；`\,` 表示字面逗号。
batchsmith::core::ListSource parse_list_option(const QString& spec, bool* ok, QString* error) {
    const int separator = spec.indexOf(QLatin1Char('='));
    if (separator <= 0) {
        *ok = false;
        *error = QStringLiteral("--list 的写法是 名=值1,值2，收到：%1").arg(spec);
        return {};
    }

    batchsmith::core::ListSource source;
    source.name = spec.left(separator);

    QStringList items;
    QString current;
    bool escaped = false;
    const QString values = spec.mid(separator + 1);
    for (const QChar ch : values) {
        if (escaped) {
            current.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char(',')) {
            items.append(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    if (escaped) {
        current.append(QLatin1Char('\\'));
    }
    items.append(current);  // 最后一个（空值也给一项，便于造空串）

    source.items = items;
    *ok = true;
    return source;
}

int run_eval(const QCommandLineParser& parser) {
    using batchsmith::core::ListPadding;
    using batchsmith::core::ListSourceList;
    using batchsmith::core::dsl::compile_template;
    using batchsmith::core::dsl::evaluate_template;

    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 2) {
        err_stream() << "用法：bs eval <模板> [--list 名=值1,值2]… [--ignore 名]…\n";
        return kExitUsage;
    }
    const QString template_text = positional.at(1);

    ListSourceList sources;
    for (const QString& spec : parser.values(QStringLiteral("list"))) {
        bool ok = false;
        QString error;
        batchsmith::core::ListSource source = parse_list_option(spec, &ok, &error);
        if (!ok) {
            err_stream() << error << "\n";
            return kExitUsage;
        }
        sources.append(source);
    }
    for (const QString& name : parser.values(QStringLiteral("ignore"))) {
        bool found = false;
        for (batchsmith::core::ListSource& source : sources) {
            if (source.name == name) {
                source.padding = ListPadding::Ignore;
                found = true;
            }
        }
        if (!found) {
            err_stream() << QStringLiteral("--ignore %1：没有这个列表\n").arg(name);
            return kExitUsage;
        }
    }

    if (parser.isSet(QStringLiteral("show-lua"))) {
        const auto compiled = compile_template(template_text);
        if (!compiled.ok()) {
            err_stream() << compiled.error << "\n";
            return kExitEvalError;
        }
        out_stream() << "# 编译产物（" << compiled.section_count << " 个区段）\n"
                     << compiled.lua_source << "\n";
    }

    const auto result = evaluate_template(template_text, sources);
    if (!result.ok()) {
        err_stream() << result.error << "\n";
        return kExitEvalError;
    }

    QTextStream& out = out_stream();
    for (const QString& row : result.rows) {
        out << row << "\n";
    }
    out.flush();
    return kExitOk;
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

    parser.addOption(QCommandLineOption(QStringLiteral("list"),
                                        QStringLiteral("列表源，形如 名=值1,值2。可重复。"),
                                        QStringLiteral("名=值")));
    parser.addOption(QCommandLineOption(QStringLiteral("ignore"),
                                        QStringLiteral("把该列表的缺省方式设为 Ignore"
                                                       "（整批行数取最短）。可重复。"),
                                        QStringLiteral("名")));
    parser.addOption(QCommandLineOption(QStringLiteral("show-lua"),
                                        QStringLiteral("同时打印编译出的 Lua 源码。")));
    parser.addOption(QCommandLineOption(QStringLiteral("html"),
                                        QStringLiteral("cheatsheet：输出 HTML 手册"
                                                       "（可用浏览器打开、便于分发）。")));
    parser.addOption(QCommandLineOption(
            QStringLiteral("hhc"), QStringLiteral("cheatsheet：输出 CHM 的目录文件（.hhc）。")));
    parser.addPositionalArgument(QStringLiteral("命令"),
                                 QStringLiteral("eval；或省略以查看用法。"));

    parser.process(app);

    // banner 走 **stderr**：stdout 只放真正的输出。
    // 否则 `bs eval … | wc -l` 会莫名多出一行，脚本消费结果时很难发现原因
    // （banner 是诊断信息，不是数据）。
    err_stream() << QString::fromLatin1(batchsmith::core::version_banner()) << "\n";

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        print_plan();
        out_stream().flush();
        return kExitUsage;
    }

    const QString command = positional.first();
    if (command == QStringLiteral("eval")) {
        return run_eval(parser);
    }
    if (command == QStringLiteral("cheatsheet") || command == QStringLiteral("help")) {
        // 与界面「帮助 → DSL 语法与函数速查」渲染的是**同一份数据**
        QTextStream& out = out_stream();
        if (parser.isSet(QStringLiteral("hhc"))) {
            // 给 CHM 用的目录文件：由同一份数据生成，不手工维护
            out << batchsmith::core::dsl::cheatsheet_hhc();
        } else if (parser.isSet(QStringLiteral("html"))) {
            out << batchsmith::core::dsl::cheatsheet_html() << "\n";
        } else {
            out << batchsmith::core::dsl::cheatsheet_text() << "\n";
        }
        out.flush();
        return kExitOk;
    }

    err_stream() << QStringLiteral("未知子命令：%1\n").arg(command);
    print_plan();
    out_stream().flush();
    return kExitUsage;
}
