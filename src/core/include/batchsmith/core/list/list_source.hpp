#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

#include "batchsmith/core/list/dir_source.hpp"

namespace batchsmith::core {

/// 列表长度不足时的缺省方式（构想书「每个列表可以配置自己的缺省方式」）。
enum class ListPadding {
    Empty,  ///< 留空：越界返回空串
    Ignore,  ///< 忽略：整批行数取所有带 Ignore 列表长度的**最小值**（截断其余列表）
    Repeat,  ///< 从头重复
};

/// 列表的**来源模式**。
///
/// 需求是「列表本身可以作为多种模式切换」—— 所以这里是"模式"而不是"两种列表"：
/// 无论从哪来，它产出的都是一列有序字符串，下游（求值、以及将来的 Plan）完全不必区分。
/// 界面上就是在同一列里切换这些模式，列名与编号都保持不变
/// （于是表达式里的 `$list1[i]$` 不会因为换了来源而失效）。
enum class ListSourceKind {
    Manual,     ///< 手输：直接键入项
    Directory,  ///< 绑定文件夹：按 `dir` 规格扫描得到项
};

/// 一个「列表源」：一列有序的字符串，外加一个供模板引用的名字。
///
/// 名字由界面分配（list1、list2…），模板里写 `$list1$` / `$list1[i]$` 引用它。
///
/// ## 规格与快照
///
/// `kind` + `dir` 是**来源规格**（从哪取数），`items` 是**当前快照**（实际参与求值的
/// 那些字符串）。手输模式下两者合一 —— 用户编辑的就是 `items`；文件夹模式下规格是用户
/// 设的路径与过滤，`items` 是上次扫描的结果，因此改了规格必须重新扫描（`refresh()`）。
///
/// 求值只读 `items`：`evaluate_template()` 因此仍是纯函数、不碰文件系统
/// （与 `bs plan` 要求零副作用是同一条理由）。
struct ListSource {
    QString name;
    ListSourceKind kind = ListSourceKind::Manual;

    /// 文件夹来源的规格（仅 `Directory` 模式使用）。其中 `path` 还是将来「行绑定路径」
    /// 的根 —— "用户看到的第 7 行对应哪个文件"，答案在这里。
    DirQuery dir;

    QStringList items;
    ListPadding padding = ListPadding::Empty;

    ListSource() = default;

    /// 手输列表 —— 最常用的构造方式，让 `ListSource{"list1", {"a", "b"}}` 能直接写。
    ListSource(QString item_name, QStringList item_values)
        : name(std::move(item_name)), items(std::move(item_values)) {}

    /// 手输列表 + 缺省方式。
    ListSource(QString item_name, QStringList item_values, ListPadding item_padding)
        : name(std::move(item_name)), items(std::move(item_values)), padding(item_padding) {}

    /// 绑定文件夹的列表：设置规格后**立刻扫描一次**。
    ///
    /// 立即扫描是为了让"绑定即取数"成为一个原子动作：界面里拖一个文件夹进来，
    /// 列上马上就能看到内容；失败时 `items` 为空，原因写进 `error`。
    /// （之后要重新取数，用 `refresh()`。）
    [[nodiscard]] static ListSource from_directory(QString item_name,
                                                   DirQuery query,
                                                   QString* error = nullptr);

    [[nodiscard]] qsizetype size() const { return items.size(); }

    /// 按缺省方式取第 index 项（**0 起**）。
    ///
    /// - `Empty`：越界返回空串
    /// - `Repeat`：越界后从头循环（列表为空时返回空串，避免除零）
    /// - `Ignore`：越界返回空串（行数已在求值时被截断，正常不会走到这里）
    ///
    /// 与 ADR-8 在 Lua 侧的行为一致：越界**返回空串而不是 nil**。
    [[nodiscard]] QString value_at(qsizetype index) const {
        if (items.isEmpty()) {
            return QString();
        }
        if (index >= 0 && index < items.size()) {
            return items.at(index);
        }
        if (padding == ListPadding::Repeat) {
            const qsizetype wrapped = ((index % items.size()) + items.size()) % items.size();
            return items.at(wrapped);
        }
        return QString();
    }
};

using ListSourceList = QList<ListSource>;

/// 按来源规格重新取数，把结果写进 `source.items`。
///
/// - `Manual`：不动 `items`（手输的内容只能由用户改），直接返回 true。
/// - `Directory`：调用 `scan_directory(source.dir)`。**失败时不改动 `items`**
///   （保留上一次成功的结果），只把原因写进 `error` —— 路径临时不可达时，
///   用户看到的是"上一次的数据 + 一条错误"，而不是整列突然空掉。
///
/// @param error 可为空。非空时失败写入原因。
/// @return 是否成功。
[[nodiscard]] bool refresh(ListSource& source, QString* error = nullptr);

}  // namespace batchsmith::core
