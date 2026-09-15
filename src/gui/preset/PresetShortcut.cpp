#include "preset/PresetShortcut.h"

#include <string>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include "batchsmith/core/preset/preset.hpp"

#if defined(Q_OS_WIN)
// 与 src/cli/src/main.cpp 一致：这两个宏要在 windows.h 之前定义，
// 否则 windows.h 会带进 min/max 宏与一大堆用不上的东西。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// WIN32_LEAN_AND_MEAN 会把 OLE 排掉，而 ShellLink 正是 OLE 那一套，所以显式包含
#include <objbase.h>
#include <shlobj.h>
#endif

namespace {

constexpr const char* kPresetOption = "--preset";

using batchsmith::core::unique_file_path;

/// 把一段命令行拆成 token：空格分隔，双引号包裹，`\x` 转义成 `x`。
///
/// 自己生成的东西本来可以不去解析，但**用户会手工改快捷方式里的这一行** ——
/// 于是读取端得按规则老实读，而不是"反正是我写的"。
///
/// `percent_is_field_code` 只在 `.desktop` 下为 true：那里 `%%` 才是字面的百分号。
[[nodiscard]] QStringList tokenize(const QString& text, bool percent_is_field_code) {
    QStringList tokens;
    QString current;
    bool in_quotes = false;
    bool has_token = false;

    const auto flush = [&tokens, &current, &has_token] {
        if (has_token) {
            tokens.append(current);
            current.clear();
            has_token = false;
        }
    };

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if (ch == QLatin1Char('\\') && i + 1 < text.size()) {
            current += text.at(i + 1);
            ++i;
            has_token = true;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            in_quotes = !in_quotes;
            has_token = true;  // `""` 是一个空参数，不是"没有参数"
            continue;
        }
        if (!in_quotes && ch.isSpace()) {
            flush();
            continue;
        }
        current += ch;
        has_token = true;
    }
    flush();

    if (percent_is_field_code) {
        for (QString& token : tokens) {
            token.replace(QLatin1String("%%"), QLatin1String("%"));
        }
    }
    return tokens;
}

[[nodiscard]] QString preset_path_in_arguments(const QString& arguments) {
    const QStringList tokens = tokenize(arguments, false);
    for (qsizetype i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens.at(i) == QLatin1String(kPresetOption)) {
            return tokens.at(i + 1);
        }
    }
    // 也接受 `--preset=xxx`：用户手改过快捷方式的话多半是这个写法
    for (const QString& token : tokens) {
        if (token.startsWith(QLatin1String("--preset="))) {
            return token.mid(static_cast<qsizetype>(qstrlen(kPresetOption)) + 1);
        }
    }
    return {};
}

#if !defined(Q_OS_WIN)

// ---------------------------------------------------------------------------
// Linux（.desktop）/ macOS（.command）—— 文本格式
// ---------------------------------------------------------------------------

/// 把 token 拼回一段规范化的命令行：含空格的参数用双引号包住。
///
/// 规范化的意义是 **`ShortcutTarget::arguments` 在三平台上长得一样**，
/// 于是测试不必写三套判断。
[[nodiscard]] QString requote(const QStringList& tokens) {
    QStringList parts;
    parts.reserve(tokens.size());
    for (const QString& token : tokens) {
        parts.append(token.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(token)
                                                      : token);
    }
    return parts.join(QLatin1Char(' '));
}

[[nodiscard]] QString quote(const QString& text) {
    return QLatin1Char('"') + text + QLatin1Char('"');
}

/// `.desktop` 的 `Exec=` 字段：引号内的内容自己有转义规则 ——
/// `\`、`"`、`` ` ``、`$` 要加反斜杠，而 `%` 是**字段码**的前导字符，
/// 字面量百分号必须写成两个。漏掉 `%` 的后果是路径被悄悄改掉，而
/// "桌面图标点不开、提示 Failed to parse" 很难联想到是百分号的问题。
[[nodiscard]] QString escape_desktop(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == QLatin1Char('%')) {
            out += QLatin1String("%%");
            continue;
        }
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"') || ch == QLatin1Char('`') ||
            ch == QLatin1Char('$')) {
            out += QLatin1Char('\\');
        }
        out += ch;
    }
    return out;
}

/// `.command` 是交给 sh 执行的一行：`$` 与反引号会被展开，必须挡住
/// （路径里带 `$` 的话，快捷方式打开的就是另一个程序了）。
/// `%` 在 sh 里是普通字符 —— **不能**照 .desktop 的规则写成 `%%`。
[[nodiscard]] QString escape_shell(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('"') || ch == QLatin1Char('`') ||
            ch == QLatin1Char('$')) {
            out += QLatin1Char('\\');
        }
        out += ch;
    }
    return out;
}

using EscapeFn = QString (*)(const QString&);

/// 把程序与参数**重新编码**成目标格式能直接用的形式。
///
/// 参数本身只有一份定义（`--preset "<绝对路径>"`，规范化命令行），
/// 各平台的差异只体现在这一层转义上 —— 否则"快捷方式里到底传了什么参数"
/// 会变成三份各自演化的事实。
[[nodiscard]] QString encode_command(const QString& program,
                                     const QString& arguments,
                                     EscapeFn escape) {
    QStringList parts;
    parts.append(quote(escape(program)));
    for (const QString& token : tokenize(arguments, false)) {
        parts.append(quote(escape(token)));
    }
    return parts.join(QLatin1Char(' '));
}

/// 让文本形式的快捷方式真的能被"双击"执行。
///
/// 失败不算致命（文件已经写出去了），但必须说出来 —— 没有可执行位的 `.desktop`
/// 在多数桌面环境里会被当成普通文本打开，用户看到的是"双击没反应"。
[[nodiscard]] QString make_executable(const QString& path) {
    const QFile::Permissions wanted = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther |
                                      QFile::ExeOther;
    if (QFile::setPermissions(path, wanted)) {
        return {};
    }
    return QStringLiteral("文件写出来了，但置可执行位失败：%1").arg(QDir::toNativeSeparators(path));
}

[[nodiscard]] bool write_text_file(const QString& path, const QString& text, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("写不了文件：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    if (file.write(text.toUtf8()) < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("写入失败：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    file.close();
    return true;
}

[[nodiscard]] QString create_desktop_entry(const QString& shortcut_path,
                                           const QString& program,
                                           const QString& arguments,
                                           const QString& name,
                                           const QString& icon,
                                           QString* error) {
    QString text = QStringLiteral("[Desktop Entry]\n"
                                  "Type=Application\n"
                                  "Version=1.0\n"
                                  "Name=%2\n"
                                  "Comment=BatchSmith 预设启动器 —— 用「%2」这套配置打开\n"
                                  "Exec=%1\n"
                                  "Terminal=false\n"
                                  "Categories=Utility;\n")
                           .arg(encode_command(program, arguments, &escape_desktop), name);
    if (!icon.isEmpty()) {
        // `.desktop` 的 Icon= 认绝对路径，也认主题里的图标名
        text += QStringLiteral("Icon=%1\n").arg(escape_desktop(icon));
    }
    if (!write_text_file(shortcut_path, text, error)) {
        return {};
    }
    const QString warning = make_executable(shortcut_path);
    if (!warning.isEmpty() && error != nullptr) {
        *error = warning;
    }
    return shortcut_path;
}

/// `.command` 用的启动脚本 —— 只有 macOS 分支会调用它。
///
/// 这个文件整体活在 `#if !defined(Q_OS_WIN)` 里面，而 `.command` 这一段在
/// macOS 之外都是死代码：Linux 上 GCC 的 `-Wunused-function`（`-Wall` 自带、
/// 且本项目对 GCC 开了 `-Werror`）会因此把**整个构建打断**。
/// 标 `maybe_unused` 而不是加平台宏 —— 意图是"这个配置下确实用不到"，
/// 而不是"这段代码不存在"；将来若 Linux 也要用，不必先去拆宏。
[[maybe_unused]] [[nodiscard]] QString create_shell_script(const QString& shortcut_path,
                                                           const QString& program,
                                                           const QString& arguments,
                                                           const QString& name,
                                                           QString* error) {
    const QString text =
            QStringLiteral("#!/bin/sh\n"
                           "# BatchSmith 预设启动器 —— 双击即用「%2」这套配置打开 BatchSmith。\n"
                           "# 由 BatchSmith 的「管理预设 → 创建快捷方式」生成，随时可以删掉。\n"
                           "exec %1\n")
                    .arg(encode_command(program, arguments, &escape_shell), name);
    if (!write_text_file(shortcut_path, text, error)) {
        return {};
    }
    const QString warning = make_executable(shortcut_path);
    if (!warning.isEmpty() && error != nullptr) {
        *error = warning;
    }
    return shortcut_path;
}

/// 从文本形式的快捷方式里读回程序、参数与图标：找第一个"看起来是命令行"的行，
/// 再找 `Icon=`。
[[nodiscard]] bool read_text_shortcut(const QString& shortcut_path,
                                      bool percent_is_field_code,
                                      ShortcutTarget* target,
                                      QString* error) {
    QFile file(shortcut_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("读不了快捷方式：%1（%2）")
                             .arg(QDir::toNativeSeparators(shortcut_path), file.errorString());
        }
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QString command;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        // `.desktop` 用 `Exec=`，`.command` 用 `exec `
        if (line.startsWith(QLatin1String("Exec="))) {
            command = line.mid(5);
        } else if (line.startsWith(QLatin1String("exec "))) {
            command = line.mid(5);
        } else if (line.startsWith(QLatin1String("Icon="))) {
            target->icon = line.mid(5);
        }
    }

    if (command.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("这个文件里没有命令行（不是本程序生成的快捷方式？）：%1")
                             .arg(QDir::toNativeSeparators(shortcut_path));
        }
        return false;
    }

    const QStringList tokens = tokenize(command, percent_is_field_code);
    if (tokens.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("快捷方式里的命令行为空：%1")
                             .arg(QDir::toNativeSeparators(shortcut_path));
        }
        return false;
    }

    target->program = tokens.first();
    target->arguments = requote(tokens.mid(1));
    target->working_directory.clear();
    return true;
}

#endif  // !Q_OS_WIN

#if defined(Q_OS_WIN)

// ---------------------------------------------------------------------------
// Windows：IShellLink + IPersistFile
// ---------------------------------------------------------------------------

/// COM 的初始化/清理。
///
/// 用 RAII 而不是手写配对，是因为下面有好几个失败返回点 —— 漏一次
/// `CoUninitialize()` 在长期运行的窗口程序里会慢慢积累引用计数。
class ComScope {
public:
    ComScope() {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_initialized = SUCCEEDED(hr);
        // RPC_E_CHANGED_MODE = 别人（比如 Qt 为了拖放）已经用另一种模式初始化过了。
        // 这不是错误：进程内的 ShellLink 对象照样能建，只是**不该**由我们来清理。
        m_usable = m_initialized || hr == RPC_E_CHANGED_MODE;
    }

    ~ComScope() {
        if (m_initialized) {
            ::CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    ComScope(ComScope&&) = delete;
    ComScope& operator=(ComScope&&) = delete;

    [[nodiscard]] bool usable() const { return m_usable; }

private:
    bool m_initialized = false;
    bool m_usable = false;
};

/// `IShellLinkW` 要的是 `const wchar_t*`，而 Qt 的 `utf16()` 给的是 `char16_t*`
/// （两者在 Windows 上都是 16 位，但类型不同）。转成本地的 `std::wstring` 比
/// `reinterpret_cast` 干净：临时对象活到语句结束，而 `SetPath` 会把内容拷走。
[[nodiscard]] std::wstring wide(const QString& text) {
    return text.toStdWString();
}

[[nodiscard]] QString hresult_text(HRESULT hr) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16);
}

/// 建一个 ShellLink 对象。失败时返回 nullptr 并写好原因。
[[nodiscard]] IShellLinkW* make_shell_link(QString* error) {
    IShellLinkW* link = nullptr;
    const HRESULT hr = ::CoCreateInstance(CLSID_ShellLink,
                                          nullptr,
                                          CLSCTX_INPROC_SERVER,
                                          IID_IShellLinkW,
                                          reinterpret_cast<void**>(&link));
    if (FAILED(hr) || link == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("创建 ShellLink 失败（%1）").arg(hresult_text(hr));
        }
        return nullptr;
    }
    return link;
}

[[nodiscard]] QString create_windows_shortcut(const QString& shortcut_path,
                                              const QString& program,
                                              const QString& arguments,
                                              const QString& description,
                                              const QString& icon,
                                              QString* error) {
    ComScope com;
    if (!com.usable()) {
        if (error != nullptr) {
            *error = QStringLiteral("COM 初始化失败，建不了快捷方式");
        }
        return {};
    }

    IShellLinkW* link = make_shell_link(error);
    if (link == nullptr) {
        return {};
    }

    const std::wstring wide_program = wide(QDir::toNativeSeparators(program));
    const std::wstring wide_arguments = wide(arguments);
    const std::wstring wide_working =
            wide(QDir::toNativeSeparators(QFileInfo(program).absolutePath()));
    const std::wstring wide_description = wide(description);
    const std::wstring wide_target = wide(QDir::toNativeSeparators(shortcut_path));
    // 图标文件不在这里校验存在性：文件没了应当退回程序自带图标（Windows 自己就是这么做的），
    // 而不是让"建快捷方式"整个失败。第二个参数 0 = 用该文件里的第 0 个图标。
    const std::wstring wide_icon = wide(QDir::toNativeSeparators(icon));

    link->SetPath(wide_program.c_str());
    link->SetArguments(wide_arguments.c_str());
    link->SetWorkingDirectory(wide_working.c_str());
    link->SetDescription(wide_description.c_str());
    if (!icon.isEmpty()) {
        link->SetIconLocation(wide_icon.c_str(), 0);
    }

    IPersistFile* file = nullptr;
    HRESULT hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (FAILED(hr) || file == nullptr) {
        link->Release();
        if (error != nullptr) {
            *error = QStringLiteral("取不到 IPersistFile（%1）").arg(hresult_text(hr));
        }
        return {};
    }

    // 第二个参数 TRUE = 立刻落盘（否则要等 Release 才写）
    hr = file->Save(wide_target.c_str(), TRUE);
    file->Release();
    link->Release();

    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = QStringLiteral("保存快捷方式失败（%1）：%2")
                             .arg(hresult_text(hr), QDir::toNativeSeparators(shortcut_path));
        }
        return {};
    }
    return shortcut_path;
}

[[nodiscard]] bool read_windows_shortcut(const QString& shortcut_path,
                                         ShortcutTarget* target,
                                         QString* error) {
    ComScope com;
    if (!com.usable()) {
        if (error != nullptr) {
            *error = QStringLiteral("COM 初始化失败，读不了快捷方式");
        }
        return false;
    }

    IShellLinkW* link = make_shell_link(error);
    if (link == nullptr) {
        return false;
    }

    IPersistFile* file = nullptr;
    HRESULT hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (FAILED(hr) || file == nullptr) {
        link->Release();
        if (error != nullptr) {
            *error = QStringLiteral("取不到 IPersistFile（%1）").arg(hresult_text(hr));
        }
        return false;
    }

    hr = file->Load(wide(QDir::toNativeSeparators(shortcut_path)).c_str(), STGM_READ);
    file->Release();
    if (FAILED(hr)) {
        link->Release();
        if (error != nullptr) {
            *error = QStringLiteral("读不了快捷方式文件（%1）：%2")
                             .arg(hresult_text(hr), QDir::toNativeSeparators(shortcut_path));
        }
        return false;
    }

    // 缓冲区给足：MAX_PATH（260）在长路径时代不够用，而截断会让"核对"这件事失去意义
    wchar_t program_buffer[4096] = {};
    wchar_t arguments_buffer[4096] = {};
    wchar_t working_buffer[4096] = {};
    wchar_t icon_buffer[4096] = {};
    int icon_index = 0;
    WIN32_FIND_DATAW find_data{};

    link->GetPath(program_buffer, 4096, &find_data, SLGP_UNCPRIORITY);
    link->GetArguments(arguments_buffer, 4096);
    link->GetWorkingDirectory(working_buffer, 4096);
    link->GetIconLocation(icon_buffer, 4096, &icon_index);
    link->Release();

    // 统一成 `/` 分隔：.lnk 里存的是 `\`，而调用方（与测试）不该各自去转
    target->program = QDir::fromNativeSeparators(QString::fromWCharArray(program_buffer));
    target->arguments = QString::fromWCharArray(arguments_buffer);
    target->working_directory = QDir::fromNativeSeparators(QString::fromWCharArray(working_buffer));
    target->icon = QDir::fromNativeSeparators(QString::fromWCharArray(icon_buffer));
    return true;
}

#endif  // Q_OS_WIN

}  // namespace

QString shortcut_suffix() {
#if defined(Q_OS_WIN)
    return QStringLiteral(".lnk");
#elif defined(Q_OS_MACOS)
    return QStringLiteral(".command");
#else
    return QStringLiteral(".desktop");
#endif
}

QString default_shortcut_directory() {
    // 桌面是"双击"这个动作最自然的地方 —— 建到预设目录里等于没建
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty() && QFileInfo(desktop).isDir()) {
        return desktop;
    }
    // 没有桌面（服务器上没有 Desktop 目录，或者被同步工具挪走了）就退回预设目录：
    // 至少是个用户点得开的地方
    return batchsmith::core::default_preset_directory();
}

QString create_preset_shortcut(const ShortcutRequest& request, QString* error) {
    const QFileInfo preset_info(request.preset_path);
    if (!preset_info.exists() || !preset_info.isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("要指向的预设不存在：%1")
                             .arg(QDir::toNativeSeparators(request.preset_path));
        }
        return {};
    }
    // 绝对路径：相对路径是相对**快捷方式所在目录**解析的，而快捷方式多半会被挪到
    // 桌面上去 —— 那时相对路径就指向别处了
    const QString target_preset = preset_info.absoluteFilePath();
    const QString target_directory =
            request.directory.isEmpty() ? default_shortcut_directory() : request.directory;

    if (!QDir().mkpath(target_directory)) {
        if (error != nullptr) {
            *error = QStringLiteral("建不了目录：%1")
                             .arg(QDir::toNativeSeparators(target_directory));
        }
        return {};
    }

    const QString program = QCoreApplication::applicationFilePath();
    if (program.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("拿不到本程序的可执行文件路径，建不了快捷方式");
        }
        return {};
    }

    const QString name = preset_info.completeBaseName();
    // 参数只有这一份定义：`--preset "<绝对路径>"`。
    // GUI 的 main.cpp 里 `--preset` 与位置参数等价，但显式选项更经得起用户手改。
    const QString arguments =
            QStringLiteral("%1 \"%2\"").arg(QLatin1String(kPresetOption), target_preset);

    const QString shortcut_path = unique_file_path(
            target_directory, name, shortcut_suffix(), QStringLiteral("BatchSmith 预设"));

#if defined(Q_OS_WIN)
    const QString description = QStringLiteral("用「%1」这个预设打开 BatchSmith").arg(name);
    return create_windows_shortcut(
            shortcut_path, program, arguments, description, request.icon_path, error);
#elif defined(Q_OS_MACOS)
    // .command 没有图标位置：这个参数被忽略（见头文件的说明），不是失败
    return create_shell_script(shortcut_path, program, arguments, name, error);
#else
    return create_desktop_entry(shortcut_path, program, arguments, name, request.icon_path, error);
#endif
}

bool read_shortcut(const QString& shortcut_path, ShortcutTarget* target, QString* error) {
    if (target == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("read_shortcut 需要一个非空的结果指针");
        }
        return false;
    }
    *target = ShortcutTarget{};

#if defined(Q_OS_WIN)
    return read_windows_shortcut(shortcut_path, target, error);
#elif defined(Q_OS_MACOS)
    return read_text_shortcut(shortcut_path, false, target, error);
#else
    return read_text_shortcut(shortcut_path, true, target, error);
#endif
}

bool ShortcutTarget::passes_preset() const {
    return !preset_path().isEmpty();
}

QString ShortcutTarget::preset_path() const {
    return preset_path_in_arguments(arguments);
}
