#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "batchsmith/core/list/list_source.hpp"

namespace batchsmith::core {

/// 预设文件的格式版本。读到**更高**版本时直接报错，而不是"尽力解析" ——
/// 猜错了会静默产出错误的批量操作，代价比拒绝加载大得多。
inline constexpr int kPresetVersion = 1;

/// 预设里的一个列表，与界面上的一列一一对应。
///
/// 与 `ListSource` 的区别：`ListSource` 是**求值时的快照 + 规格**（`items` 已经取好数），
/// 而 `PresetList` 是**存盘的形式** —— 文件夹路径里可以带 `${槽位}`，`items` 也可能为空
/// （文件夹来源的列表不需要把条目存进文件，加载时重新扫一次即可，文件因此不会过时）。
struct PresetList {
    QString id;  ///< list1、list2…（表达式靠它引用）
    ListSourceKind kind = ListSourceKind::Manual;
    QStringList items;  ///< 仅 `Manual` 模式：手输的条目
    DirQuery dir;       ///< 仅 `Directory` 模式：取数规格（path 可含槽位）
    ListPadding fill = ListPadding::Empty;
};

/// 一个预设。
struct Preset {
    QString name;
    int version = kPresetVersion;
    /// `safe` / `advanced` —— 只作来源标记与界面提示，**不做分级引擎**
    /// （技术方案 ADR-5：安全来自沙箱与两阶段执行，不来自把功能藏起来）。
    QString level = QStringLiteral("safe");

    QList<PresetList> lists;
    QString template_text;

    /// 输出方式。当前只支持 `rename`（内置重命名，不经 shell）。
    ///
    /// 字段现在就读、也校验，但**执行**属于 Phase 4 —— 校验是为了让
    /// `mode = "argv"` 这种还没实现的写法当场报错，而不是被静默忽略后
    /// 让用户以为"配了就能用"。
    QString output_mode = QStringLiteral("rename");

    /// 从哪个文件读来的（**不写进文件**）。空表示还没保存过。
    QString file_path;

    [[nodiscard]] bool is_valid() const { return !lists.isEmpty() || !template_text.isEmpty(); }
};

/// 槽位绑定：`${input}` → 实际路径。
///
/// 这是**本机信息**，所以存在预设旁边的 `<预设名>.local.toml` 里，而不是预设文件里 ——
/// 预设因此可以进版本控制、可以发给别人，而路径不会跟着泄漏。分享时不要带 `.local.toml`。
using SlotBindings = QHash<QString, QString>;

// ---------------------------------------------------------------------------
// 槽位
// ---------------------------------------------------------------------------

/// 取出文本里出现的槽位名（按出现顺序、去重）。语法：`${名字}`，名字限 `[A-Za-z0-9_]`。
[[nodiscard]] QStringList slots_in(QStringView text);

/// 取出预设里所有列表用到的槽位（按列表顺序、去重）。
[[nodiscard]] QStringList preset_slots(const Preset& preset);

/// 把 `${槽位}` 替换成绑定值。
///
/// **未绑定的槽位原样保留** —— 这样它会在扫描时报「文件夹不存在：${input}」，
/// 用户一眼看得出是"还没绑定"，而不是被换成空路径后报一个莫名其妙的错。
[[nodiscard]] QString bind_slots(QStringView text, const SlotBindings& bindings);

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------

/// 序列化成 TOML 文本（带注释，可手改）。
[[nodiscard]] QString preset_to_toml(const Preset& preset);

struct PresetLoad {
    Preset preset;
    QString error;  ///< 非空即失败

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// 从 TOML 文本解析。
[[nodiscard]] PresetLoad preset_from_toml(const QString& text, const QString& source_name = {});

// ---------------------------------------------------------------------------
// 文件（预设 + 伴生绑定文件）
// ---------------------------------------------------------------------------

/// 预设文件的伴生绑定文件路径：`foo.toml` → `foo.local.toml`。
[[nodiscard]] QString bindings_path_for(const QString& preset_path);

/// 用户预设目录（`<AppData>/BatchSmith/presets` 之类，各平台按各自惯例）。
///
/// **只返回路径，不创建目录** —— 查询不该有副作用；真正要写文件之前
/// 调 `ensure_preset_directory()`。
[[nodiscard]] QString default_preset_directory();

/// 确保预设目录存在（要保存预设之前调用）。失败时把原因写进 `error`。
[[nodiscard]] bool ensure_preset_directory(QString* error = nullptr);

/// 列出预设目录里的预设文件（按文件名排序）。目录不存在时返回空表，不算错。
[[nodiscard]] QStringList list_preset_files();

/// 在 `directory` 里挑一个**还空着**的文件名，返回完整路径。
///
/// `base_name` 先被净化：路径分隔符与文件系统非法字符换成下划线、首尾空白与
/// 结尾的点去掉；结果为空（或净化后只剩 `.local` 这类）就退回 `fallback`。
/// **以 `.local` 结尾的名字会被去掉那个后缀** —— 那个后缀是留给本机绑定文件的，
/// 拿它当预设名会导致文件下次读不出来（`list_preset_files()` 会跳过它）。
///
/// 然后按 `名字.ext`、`名字 2.ext`、`名字 3.ext`… 依次试，最多到 999。
///
/// 为什么放在 core 而不是各调用点各写一遍：预设文件与"启动即加载"的快捷方式
/// 都要这套（一个要 `*.toml`、一个要 `*.lnk` / `*.desktop`），而这类净化/去重
/// 逻辑一旦分家就必然漂移 —— 比如某处忘了拒绝 `.local`，用户就会存出一个
/// 自己打不开的预设。
[[nodiscard]] QString unique_file_path(const QString& directory,
                                       const QString& base_name,
                                       const QString& suffix,
                                       const QString& fallback = QStringLiteral("未命名"));

[[nodiscard]] bool save_preset(const Preset& preset, const QString& path, QString* error);
[[nodiscard]] PresetLoad load_preset(const QString& path);

/// 绑定文件的读写。
///
/// 文件不存在**不算失败**（还没绑过而已），返回空表、不设 error；
/// 文件存在但读不动/不是合法 TOML 时才设 `error` —— 那种情况必须让用户知道，
/// 否则"绑定莫名其妙丢了"极难排查。
[[nodiscard]] bool save_bindings(const SlotBindings& bindings,
                                 const QString& preset_path,
                                 QString* error);
[[nodiscard]] SlotBindings load_bindings(const QString& preset_path, QString* error = nullptr);

// ---------------------------------------------------------------------------
// 与运行时表示的互转
// ---------------------------------------------------------------------------

/// 把预设变成可直接求值的列表源：槽位替换 + 文件夹扫描。
///
/// 每个列表都会 `refresh()`（`Manual` 不动内容）。扫描失败的列表**保留为
/// 空内容**，并把原因写进 `errors`（key = 列表 id）—— 加载预设时"有一列读不到"
/// 不该让整个预设打不开，用户往往只想改个路径。
[[nodiscard]] ListSourceList preset_sources(const Preset& preset,
                                            const SlotBindings& bindings,
                                            QHash<QString, QString>* errors = nullptr);

/// 把界面上的列表源存成预设：**文件夹路径会槽位化**。
///
/// 规则（写在代码里以免以后被无声改掉）：**同一个路径共用一个槽位名**，
/// 依次为 `input`、`input2`、`input3`…。于是"两个列表绑同一个文件夹、只是过滤不同"
/// 这种常见写法在文件里只出现一次路径，改绑定也就只改一处。
///
/// 槽位值由 `bindings` 带出（调用方负责写进伴生文件）。
[[nodiscard]] Preset preset_from_sources(const ListSourceList& sources,
                                         QString template_text,
                                         const QString& name,
                                         SlotBindings* bindings);

}  // namespace batchsmith::core
