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
#include "batchsmith/core/plan/plan.hpp"
#include "batchsmith/core/preset/preset.hpp"
#include "batchsmith/core/version.hpp"

#if defined(BATCHSMITH_CRASH_REPORT) && defined(Q_OS_WIN)
/// 崩溃自报（诊断构建专用，见 crash_report.cpp）。
/// ⚠️ 必须放在**文件作用域**（不能塞进下面的匿名 namespace）：它的定义在另一个
/// 编译单元里，进了匿名 namespace 就变成内部链接 ⇒ 直接 undefined reference。
void install_crash_reporter();
#endif

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

/// `bs plan` 的两条额外退出码。**分开是有意的**：脚本要能区分
/// "计划算出来了但里面有不能执行的行（要给人看）"与"计划根本没算出来（要看报错）"。
constexpr int kExitPlanProblems = 1;
constexpr int kExitPlanFailed = 3;

void print_plan() {
    QTextStream& out = out_stream();
    out << "\n"
        << "可用子命令：\n"
        << "  bs eval <模板> [--list 名=值1,值2]… [--ignore 名]… [--show-lua]\n"
        << "        只跑 DSL 编译与沙箱求值，**不接触文件系统**。输出为逐行结果。\n"
        << "        --list 也可以绑定文件夹：\n"
        << "          --list 'list1=@dir:D:/anime'\n"
        << "          --list 'list1=@dir:D:/anime;filter=*.mkv;recursive=1'\n"
        << "        选项：filter=<glob>（多个用 ; 或 , 分隔，作用于条目名）\n"
        << "              recursive=1 递归子目录   dirs=1 把子目录也算条目\n"
        << "              hidden=1 含以 . 开头的条目\n"
        << "        条目按**自然序**排列（file2 在 file10 之前），值为相对该文件夹的路径。\n"
        << "  bs plan <预设.toml> [--bind 槽位=路径]… [--json]\n"
        << "        算出这次批量操作会做些什么并打印出来（哪一行 → 哪个新名字），\n"
        << "        **只读、不产生任何副作用** —— 这是 Phase 4 的 apply 之前必须过的一步。\n"
        << "        绑定：先读预设旁边的 <预设名>.local.toml（界面里绑过就在这里），\n"
        << "              再用 --bind 覆盖它（命令行写的那个更明确）。\n"
        << "        --json 输出机器可读的计划（脚本用；状态名是稳定的 ASCII 词）。\n"
        << "        退出码：0 全部可执行；1 有不可执行的行（请看输出）；\n"
        << "                2 用法错；3 计划没算出来（预设读不动、路径不通、模板对不上…）。\n"
        << "  bs cheatsheet [--html | --hhc]\n"
        << "        打印 DSL 语法、工具函数与示例（与界面的「帮助」同一份内容）；\n"
        << "        --html 输出可独立打开的 HTML 手册；\n"
        << "        --hhc  输出 CHM 的目录文件（配 packaging/make-chm.sh 打 .chm）。\n"
        << "\n"
        << "尚未实现（见 docs/技术方案与实现路线.md §7）：\n"
        << "  bs run   <预设> --bind k=v   执行；默认 dry-run，需 --apply 才真正落盘（Phase 4）\n"
        << "  bs undo  <日志>              按撤销日志逆序回放（Phase 4）\n";
}

/// 解析 `recursive=1` 这类开关的取值。`recursive`（无 `=`）也算打开。
bool parse_switch(const QString& value, bool* ok) {
    const QString lowered = value.trimmed().toLower();
    if (lowered.isEmpty() || lowered == QLatin1String("1") || lowered == QLatin1String("true") ||
        lowered == QLatin1String("yes") || lowered == QLatin1String("on")) {
        *ok = true;
        return true;
    }
    if (lowered == QLatin1String("0") || lowered == QLatin1String("false") ||
        lowered == QLatin1String("no") || lowered == QLatin1String("off")) {
        *ok = true;
        return false;
    }
    *ok = false;
    return false;
}

/// `@dir:<文件夹>[;<选项>]…` → 绑定文件夹的列表源。
///
/// 语法刻意做得紧凑：CLI 的定位是脚本与验证，一个列表的完整来源应当能写在一行里。
/// 路径取到第一个 `;` 之前，因此路径里的 `=` 不会干扰解析。
batchsmith::core::ListSource parse_dir_option(const QString& name,
                                              const QString& body,
                                              bool* ok,
                                              QString* error) {
    batchsmith::core::DirQuery query;

    const QStringList parts = body.split(QLatin1Char(';'));
    query.path = parts.first().trimmed();

    for (qsizetype index = 1; index < parts.size(); ++index) {
        const QString piece = parts.at(index).trimmed();
        if (piece.isEmpty()) {
            continue;
        }
        const int separator = piece.indexOf(QLatin1Char('='));
        const QString key = (separator < 0 ? piece : piece.left(separator)).trimmed().toLower();
        const QString value = (separator < 0 ? QString() : piece.mid(separator + 1));

        bool parsed = false;
        if (key == QLatin1String("filter")) {
            query.filter = value;
        } else if (key == QLatin1String("recursive")) {
            query.recursive = parse_switch(value, &parsed);
            if (!parsed) {
                *ok = false;
                *error =
                        QStringLiteral("--list %1：recursive 只认 1/0（收到 %2）").arg(name, value);
                return {};
            }
        } else if (key == QLatin1String("dirs")) {
            query.include_dirs = parse_switch(value, &parsed);
            if (!parsed) {
                *ok = false;
                *error = QStringLiteral("--list %1：dirs 只认 1/0（收到 %2）").arg(name, value);
                return {};
            }
        } else if (key == QLatin1String("hidden")) {
            query.include_hidden = parse_switch(value, &parsed);
            if (!parsed) {
                *ok = false;
                *error = QStringLiteral("--list %1：hidden 只认 1/0（收到 %2）").arg(name, value);
                return {};
            }
        } else {
            *ok = false;
            *error = QStringLiteral("--list %1：不认识的选项「%2」"
                                    "（可用 filter / recursive / dirs / hidden）")
                             .arg(name, key);
            return {};
        }
    }

    // 路径不通时直接失败，不给一个空列表 —— 空列表会让"批量操作什么都没做"
    // 看起来像正常结果。
    QString scan_error;
    batchsmith::core::ListSource source =
            batchsmith::core::ListSource::from_directory(name, query, &scan_error);
    if (!scan_error.isEmpty()) {
        *ok = false;
        *error = QStringLiteral("--list %1：%2").arg(name, scan_error);
        return {};
    }

    *ok = true;
    return source;
}

/// `--list 名=值1,值2` → 列表源。逗号分隔；`\,` 表示字面逗号。
/// 值以 `@dir:` 开头时改为绑定文件夹（见 parse_dir_option）。
batchsmith::core::ListSource parse_list_option(const QString& spec, bool* ok, QString* error) {
    const int separator = spec.indexOf(QLatin1Char('='));
    if (separator <= 0) {
        *ok = false;
        *error = QStringLiteral("--list 的写法是 名=值1,值2 或 名=@dir:<文件夹>，收到：%1")
                         .arg(spec);
        return {};
    }

    const QString name = spec.left(separator);
    const QString body = spec.mid(separator + 1);
    if (body.startsWith(QLatin1String("@dir:"))) {
        return parse_dir_option(name, body.mid(5), ok, error);
    }

    batchsmith::core::ListSource source;
    source.name = name;

    QStringList items;
    QString current;
    bool escaped = false;
    for (const QChar ch : body) {
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
        err_stream() << "用法：bs eval <模板> [--list 名=值1,值2 或 名=@dir:<文件夹>]… "
                        "[--ignore 名]…\n";
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

/// `--bind 槽位=路径` → 槽位绑定。可重复。
bool parse_bind_options(const QCommandLineParser& parser,
                        batchsmith::core::SlotBindings* bindings,
                        QString* error) {
    for (const QString& spec : parser.values(QStringLiteral("bind"))) {
        const int separator = spec.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            *error = QStringLiteral("--bind 的写法是 槽位名=路径，收到：%1").arg(spec);
            return false;
        }
        const QString name = spec.left(separator).trimmed();
        const QString path = spec.mid(separator + 1).trimmed();
        if (name.isEmpty() || path.isEmpty()) {
            *error = QStringLiteral("--bind 的槽位名与路径都不能为空，收到：%1").arg(spec);
            return false;
        }
        bindings->insert(name, path);
    }
    return true;
}

int run_plan(const QCommandLineParser& parser) {
    using batchsmith::core::load_bindings;
    using batchsmith::core::load_preset;
    using batchsmith::core::PresetLoad;
    using batchsmith::core::SlotBindings;
    using batchsmith::core::plan::build_plan;
    using batchsmith::core::plan::plan_to_json;
    using batchsmith::core::plan::plan_to_text;

    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 2) {
        err_stream() << "用法：bs plan <预设.toml> [--bind 槽位=路径]… [--json]\n";
        return kExitUsage;
    }
    const QString preset_path = positional.at(1);

    const PresetLoad loaded = load_preset(preset_path);
    if (!loaded.ok()) {
        err_stream() << loaded.error << "\n";
        return kExitPlanFailed;
    }

    // 绑定：先读预设旁边那份 `.local.toml`（界面里绑过的话路径就在这里），
    // 再用 `--bind` 覆盖它。覆盖方向是有意的 —— 命令行写的是这次**明确要求**的，
    // 文件里那份是"上次留下的"。
    QString bind_error;
    SlotBindings bindings = load_bindings(preset_path, &bind_error);
    if (!bind_error.isEmpty()) {
        err_stream() << QStringLiteral("读不到绑定文件：%1\n").arg(bind_error);
        return kExitPlanFailed;
    }
    QString parse_error;
    if (!parse_bind_options(parser, &bindings, &parse_error)) {
        err_stream() << parse_error << "\n";
        return kExitUsage;
    }

    const auto built = build_plan(loaded.preset, bindings);
    if (!built.ok()) {
        err_stream() << built.error << "\n";
        return kExitPlanFailed;
    }

    QTextStream& out = out_stream();
    if (parser.isSet(QStringLiteral("json"))) {
        out << plan_to_json(built.plan) << "\n";
    } else {
        out << plan_to_text(built.plan) << "\n";
    }
    out.flush();

    // 有不可执行的行就返回非零 —— dry-run 要报告的正是这件事；
    // 脚本据此决定"能不能往下走"，而不必去解析给人看的文本。
    return built.plan.has_problems() ? kExitPlanProblems : kExitOk;
}

}  // namespace

int main(int argc, char* argv[]) {
#if defined(Q_OS_WIN)
    // Windows 控制台默认不是 UTF-8，不设这一行中文输出会乱码
    SetConsoleOutputCP(CP_UTF8);
#endif

#if defined(BATCHSMITH_CRASH_REPORT) && defined(Q_OS_WIN)
    // **第一件事**就是装崩溃自报：要赶在任何 Sandbox 建立之前。
    // 实现与"为什么不再绕 WER"写在 src/cli/src/crash_report.cpp 的头部。
    install_crash_reporter();
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

    parser.addOption(
            QCommandLineOption(QStringLiteral("list"),
                               QStringLiteral("列表源：名=值1,值2（手输）或 "
                                              "名=@dir:<文件夹>[;filter=<glob>][;recursive=1]"
                                              "[;dirs=1][;hidden=1]（绑定文件夹）。可重复。"),
                               QStringLiteral("名=值")));
    parser.addOption(QCommandLineOption(QStringLiteral("ignore"),
                                        QStringLiteral("把该列表的缺省方式设为 Ignore"
                                                       "（整批行数取最短）。可重复。"),
                                        QStringLiteral("名")));
    parser.addOption(QCommandLineOption(QStringLiteral("show-lua"),
                                        QStringLiteral("同时打印编译出的 Lua 源码。")));
    parser.addOption(QCommandLineOption(QStringLiteral("bind"),
                                        QStringLiteral("plan：把预设里的槽位绑到实际路径"
                                                       "（如 --bind input=D:/动画）。可重复，"
                                                       "覆盖伴生 .local.toml 里的绑定。"),
                                        QStringLiteral("槽位=路径")));
    parser.addOption(QCommandLineOption(QStringLiteral("json"),
                                        QStringLiteral("plan：输出机器可读的 JSON"
                                                       "（状态名是稳定的 ASCII 词）。")));
    parser.addOption(QCommandLineOption(QStringLiteral("html"),
                                        QStringLiteral("cheatsheet：输出 HTML 手册"
                                                       "（可用浏览器打开、便于分发）。")));
    parser.addOption(QCommandLineOption(
            QStringLiteral("hhc"), QStringLiteral("cheatsheet：输出 CHM 的目录文件（.hhc）。")));
    parser.addPositionalArgument(QStringLiteral("命令"),
                                 QStringLiteral("eval / plan；或省略以查看用法。"));

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
    if (command == QStringLiteral("plan")) {
        return run_plan(parser);
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
