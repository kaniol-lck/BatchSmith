#include "batchsmith/core/list/list_source.hpp"

#include <utility>

namespace batchsmith::core {

ListSource ListSource::from_directory(QString item_name, DirQuery query, QString* error) {
    ListSource source;
    source.name = std::move(item_name);
    source.kind = ListSourceKind::Directory;
    source.dir = std::move(query);
    // 失败不在这里报：items 会是空的，原因已经通过 error 带回给调用方。
    [[maybe_unused]] const bool refreshed = refresh(source, error);
    return source;
}

bool refresh(ListSource& source, QString* error) {
    switch (source.kind) {
        case ListSourceKind::Manual:
            // 手输的内容只能由用户改 —— 这里什么都不做，也不算失败。
            return true;

        case ListSourceKind::Directory: {
            const DirScan scan = scan_directory(source.dir);
            if (!scan.ok()) {
                // 刻意不覆盖 items：路径临时不可达（U 盘拔了、网络盘掉线）时，
                // 列里保留上一次的数据加上一条错误，比整列清空更好排查。
                if (error != nullptr) {
                    *error = scan.error;
                }
                return false;
            }
            source.items = scan.items;
            return true;
        }
    }

    if (error != nullptr) {
        *error = QStringLiteral("未知的列表来源模式");
    }
    return false;
}

}  // namespace batchsmith::core
