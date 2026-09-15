#pragma once

#include <utility>

#include <QString>

/// 「双击就用某个预设打开 BatchSmith」的快捷方式。
///
/// 为什么要替用户建这个东西：预设的全部价值就是"这套参数固定下来了"，而固定下来的
/// 东西最自然的用法是**双击**。让用户自己右键 → 新建快捷方式 → 去翻 exe 在哪 →
/// 再手打 `--preset "…"`，中间任何一步错了，结果都只是"打开了一个空白窗口"，
/// 而且界面上看不出哪里错了。这正好把预设的意义抵消掉了。
///
/// 各平台的产物不同，因为"双击能跑"这件事在各平台的载体本来就不同：
///
/// | 平台 | 产物 | 说明 |
/// |---|---|---|
/// | Windows | `.lnk` | 走 `IShellLink` + `IPersistFile`，与"右键新建快捷方式"同一种东西 |
/// | Linux | `.desktop` | 桌面环境认它；要置可执行位 |
/// | macOS | `.command` | Finder 里双击可执行。**不是** `.app` 包 —— 那需要 Info.plist、
///   图标与代码签名，为一个启动参数去造一个 bundle 不划算，而 `.command` 是诚实的 |
///
/// 三者共用一个契约（见下面的两个函数），平台差异只留在实现里 —— 于是调用方
/// （管理对话框）与测试都不需要知道当前在哪。
///
/// 与 GUI 层其它文件一致，不放命名空间（`batchsmith::core` 那些才是库的一部分）。
///
/// 三个路径字段统一用 `/` 分隔（与 Qt 的惯例一致）：`.lnk` 里存的本来是 `\`，
/// 让每个调用方各自去 `fromNativeSeparators` 只会漏掉某处。
struct ShortcutTarget {
    QString program;            ///< 被启动的程序（Windows 下是 exe 的完整路径）
    QString arguments;          ///< 参数**规范化**后的原文；含空格的参数一定是
                                ///< 双引号包着的（见 read_shortcut 的说明）
    QString working_directory;  ///< 工作目录（文本格式的快捷方式没有这一项，留空）
    QString icon;               ///< 自定义图标；空表示用程序自带图标

    /// 参数里是否带着 `--preset <路径>` 形式的预设指定
    [[nodiscard]] bool passes_preset() const;

    /// 取出 `--preset` 后面的那个路径（没写就返回空）
    [[nodiscard]] QString preset_path() const;
};

/// 要建什么样的快捷方式。
///
/// 用结构体而不是一串可选参数：这些都是彼此独立的可选设置，摊成参数列表之后
/// 调用点会变成 `create(a, b, nullptr, QString(), true)` 这种谁也读不懂的样子。
///
/// 给了构造函数（而不是让它当聚合体）：`ShortcutRequest{a, b}` 这种只写前两个字段的
/// 写法在 `-Wextra` 下会被 `-Wmissing-field-initializers` 拦下来，而那个警告是**对的** ——
/// 少写一个字段时到底是有意省略还是忘了，读代码的人分不清。
struct ShortcutRequest {
    explicit ShortcutRequest(QString preset, QString directory_to_use = {}, QString icon = {})
        : preset_path(std::move(preset)), directory(std::move(directory_to_use)),
          icon_path(std::move(icon)) {}

    QString preset_path;  ///< 指向哪个预设（必填，内部会转成绝对路径）
    QString directory;    ///< 建到哪个目录；空 = `default_shortcut_directory()`
    QString icon_path;    ///< 快捷方式用哪个图标；空 = 程序自带图标
};

/// 本平台上快捷方式的扩展名：`.lnk` / `.desktop` / `.command`。
[[nodiscard]] QString shortcut_suffix();

/// 默认把快捷方式建到哪：桌面；桌面拿不到（没有 Desktop 目录、只读）就退回预设目录。
///
/// **只返回路径，不创建目录**。
[[nodiscard]] QString default_shortcut_directory();

/// 创建"带 `--preset` 参数启动"的快捷方式，返回建出来的完整路径；
/// 失败时返回空并把原因写进 `error`。
///
/// `preset_path` 会被转成**绝对路径**：快捷方式里的相对路径是相对快捷方式自己
/// 所在目录解析的，写相对路径会随快捷方式被挪动而失效 —— 而"挪到桌面"正是
/// 用户接下来一定会做的事。
///
/// 重名不覆盖：交给 `core::unique_file_path()` 加序号（见那儿的说明）。
///
/// 图标：Windows 走 `IShellLink::SetIconLocation`，Linux 写 `.desktop` 的 `Icon=`；
/// **macOS 的 `.command` 没有图标位置，这个参数会被忽略**（要图标得做 `.app` 包，
/// 那需要 Info.plist 与图标资源，代价不成比例）—— 忽略的事在文档里写清楚，
/// 而不是假装成功。
[[nodiscard]] QString create_preset_shortcut(const ShortcutRequest& request,
                                             QString* error = nullptr);

/// 读回快捷方式指向的程序与参数。
///
/// 有这个函数是为了**能自证**：快捷方式建出来之后，"它真的指向本程序的 exe、
/// 且带着对的预设路径"必须能被核对。建一个指向空气的快捷方式，比什么都不建更糟。
///
/// `target->arguments` 是**规范化**过的：各平台的原始形态不同（`.lnk` 存的是一段
/// 命令行文本、`.desktop` 有自己的一套转义、`.command` 是 sh 语法），这里统一成
/// "按空格分开、含空格的参数用双引号包起来"这一种形态 —— 于是测试与调用方
/// 不必写三套判断。
[[nodiscard]] bool read_shortcut(const QString& shortcut_path,
                                 ShortcutTarget* target,
                                 QString* error = nullptr);
