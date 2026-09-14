#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace batchsmith::core {

/// 列表长度不足时的缺省方式（构想书「每个列表可以配置自己的缺省方式」）。
enum class ListPadding {
    Empty,  ///< 留空：越界返回空串
    Ignore,  ///< 忽略：整批行数取所有带 Ignore 列表长度的**最小值**（截断其余列表）
    Repeat,  ///< 从头重复
};

/// 一个「列表源」：一列有序的字符串，外加一个供模板引用的名字。
///
/// 名字由界面分配（list1、list2…），模板里写 `$list1$` / `$list1[i]$` 引用它。
struct ListSource {
    QString name;
    QStringList items;
    ListPadding padding = ListPadding::Empty;

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

}  // namespace batchsmith::core
