#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace batchsmith::core {

/// 一个「列表源」：一列有序的字符串，外加一个供模板引用的名字。
///
/// 名字由界面分配（list1、list2…），模板里写 `$list1$` 引用它。
/// 界面上的每一列就是一个 ListSource。
struct ListSource {
    QString name;
    QStringList items;

    /// 取某项；越界返回空串（技术方案 §3.2 的「补位用普通表」语义）。
    [[nodiscard]] QString at(qsizetype index) const {
        return index >= 0 && index < items.size() ? items.at(index) : QString();
    }
};

using ListSourceList = QList<ListSource>;

}  // namespace batchsmith::core
