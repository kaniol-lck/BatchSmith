#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>
#include <Qt>

namespace batchsmith::core {

/// glob 匹配，作用于**单个条目名**（文件名或目录名），不是整条路径。
///
/// 支持 `*`（任意长度，可为 0）、`?`（恰好一个字符）、`[abc]` / `[a-z]` / `[!abc]`
/// （字符集，`!` 与 `^` 都表示取反）、`\x`（字面 x）。
///
/// **不跨目录分隔符** —— 本函数只比较单个条目名，目录层级完全由 `recursive` 开关决定。
/// 这是刻意的：若允许 `sub/*.mkv` 这类横跨层级的模式，就会出现"到底匹配相对路径还是
/// 文件名"的二义，而递归开关已经能表达同一件事。于是"过滤匹配什么"只有一个答案。
///
/// **大小写默认不敏感。** 也刻意：Windows 与 macOS 的文件系统默认不敏感，而
/// `*.MKV` 匹配不到 `a.mkv` 会让同一份预设在不同平台给出不同结果 ——
/// 同一份输入必须给出同一个答案（与 `natural_compare` 拒绝跟随系统区域设置同理）。
[[nodiscard]] bool glob_match(QStringView pattern,
                              QStringView name,
                              Qt::CaseSensitivity cs = Qt::CaseInsensitive);

/// 绑定文件夹时的取数规格。
///
/// 这是"列表的另一种来源"，与手输并列：两者产出的是同一种东西（一列有序的字符串），
/// 所以下游（求值、将来的 Plan）完全不必知道列表是从哪来的。
struct DirQuery {
    /// 文件夹路径。扫描前必须是**已解析的实际路径**（`${槽位}` 由调用方先绑定好）——
    /// 本模块不做槽位解析，它只认路径，这样"绑定"这件事只有一处实现。
    QString path;

    /// glob 过滤，作用于条目名；`;` 或 `,` 分隔多个（**任一匹配即可**）；空表示不过滤。
    ///
    /// 过滤对**目录条目**同样生效（当 `include_dirs` 打开时）—— 否则"含目录"与
    /// "过滤"两条规则会互相打架，出现"过滤了但又没完全过滤"的情况。
    QString filter;

    /// 是否递归子目录。条目因此会带上相对路径（`第01话/正片.mkv`）。
    bool recursive = false;

    /// 是否把子目录也当作条目（例如"一集一个文件夹"的番剧目录）。
    bool include_dirs = false;

    /// 是否包含以 `.` 开头的条目（`.git`、`.DS_Store` 等）。默认排除。
    ///
    /// 判断依据是**名字以 `.` 开头**，而不是平台的文件属性位：Windows 的"隐藏属性"
    /// 在 Unix 上不存在，跟随它会让同一份预设在不同平台扫出不同的条目。
    /// （同理，目录的符号链接一律不进入 —— 那会成环。）
    bool include_hidden = false;
};

/// 扫描结果。
struct DirScan {
    /// 条目：**相对于 `DirQuery::path` 的路径**，分隔符统一为 `/`。
    ///
    /// 为什么给相对路径而不是绝对路径：列表值会直接进模板（`$list1[i]$`），
    /// 用户看的是"文件名"；需要绝对路径时 `$join(根, list1[i])$` 即可。
    /// 分隔符统一 `/` 则让同一份预设在任何平台都写成同一个样子。
    QStringList items;

    /// 非空即失败。**不存在、不是文件夹、无权限都会报错**，而不是给一个空列表 ——
    /// 路径绑错时的空列表（批量操作里意味着"什么都不会发生"或"整批行数为 0"）
    /// 是最难排查的一类问题。
    QString error;

    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

/// 按规格扫描文件夹。条目按**自然序**排列（`file2` 在 `file10` 之前）。
///
/// 排序**不提供开关**：`file10` 排在 `file2` 前面时，没人能凭肉眼核对"第 7 行是不是
/// 我想到的那个文件"，而这条路线的安全模型正依赖用户能直观核对行与文件的对应关系
/// （见 docs/技术方案与实现路线.md §4.4 / §4.6）。给个 `sort=none` 的选项只会让人
/// 在一半的平台上得到一半正确的结果。
[[nodiscard]] DirScan scan_directory(const DirQuery& query);

}  // namespace batchsmith::core
