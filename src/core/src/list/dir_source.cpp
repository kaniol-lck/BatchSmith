#include "batchsmith/core/list/dir_source.hpp"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "batchsmith/core/text/natural_compare.hpp"

namespace batchsmith::core {
namespace {

/// 单个字符比较（受大小写开关控制）。
bool char_equal(QChar a, QChar b, Qt::CaseSensitivity cs) {
    if (cs == Qt::CaseSensitive) {
        return a == b;
    }
    return a.toCaseFolded() == b.toCaseFolded();
}

/// `lo-hi` 区间是否包含 `c`。区间两端先各自折叠大小写再比较，
/// 于是 `[a-z]` 在忽略大小写时也能匹配 `A`。
bool range_contains(QChar lo, QChar hi, QChar c, Qt::CaseSensitivity cs) {
    const QChar folded = (cs == Qt::CaseInsensitive) ? c.toCaseFolded() : c;
    const QChar low = (cs == Qt::CaseInsensitive) ? lo.toCaseFolded() : lo;
    const QChar high = (cs == Qt::CaseInsensitive) ? hi.toCaseFolded() : hi;
    return folded >= low && folded <= high;
}

/// 解析并匹配一个 `[...]` 字符集。
///
/// @param open  `[` 的下标
/// @param after 输出：字符集结束（`]`）之后的下标；**为 0 表示这不是一个合法的字符集**，
///              调用方应把 `[` 当字面字符处理（`[` 没有配对的 `]` 是常见笔误，
///              整条模式直接失效比"匹配不到"更难排查）。
/// @return 该字符集是否匹配 `c`
bool match_class(
        QStringView pattern, qsizetype open, QChar c, qsizetype* after, Qt::CaseSensitivity cs) {
    qsizetype i = open + 1;
    bool negate = false;
    if (i < pattern.size() && (pattern.at(i) == u'!' || pattern.at(i) == u'^')) {
        negate = true;
        ++i;
    }

    bool matched = false;
    bool first = true;  // `[]]` 里的第一个 `]` 是字面字符，不是结束符
    while (i < pattern.size()) {
        if (pattern.at(i) == u']' && !first) {
            *after = i + 1;
            return negate ? !matched : matched;
        }
        first = false;

        QChar lo = pattern.at(i);
        if (lo == u'\\' && i + 1 < pattern.size()) {
            ++i;
            lo = pattern.at(i);
        }

        // `lo-hi` 区间（末尾的 `-` 是字面字符，如 `[a-]`）
        if (i + 2 < pattern.size() && pattern.at(i + 1) == u'-' && pattern.at(i + 2) != u']') {
            QChar hi = pattern.at(i + 2);
            if (hi == u'\\' && i + 3 < pattern.size()) {
                ++i;
                hi = pattern.at(i + 2);
            }
            if (range_contains(lo, hi, c, cs)) {
                matched = true;
            }
            i += 3;
            continue;
        }

        if (char_equal(lo, c, cs)) {
            matched = true;
        }
        ++i;
    }

    *after = 0;  // 没有配对的 `]`
    return false;
}

bool match_here(
        QStringView pattern, qsizetype pi, QStringView name, qsizetype ni, Qt::CaseSensitivity cs) {
    while (pi < pattern.size()) {
        const QChar pc = pattern.at(pi);

        if (pc == u'*') {
            while (pi < pattern.size() && pattern.at(pi) == u'*') {
                ++pi;  // 连续的 `*` 等价于一个
            }
            if (pi == pattern.size()) {
                // 末尾的 `*` 吸收剩下全部，但同样不吃 `/`
                for (qsizetype k = ni; k < name.size(); ++k) {
                    if (name.at(k) == u'/') {
                        return false;
                    }
                }
                return true;
            }
            // `*` 与 `?` 都不吃 `/`：这样"模式只作用于单个条目名"是实现的保证，
            // 而不只是调用方的约定 —— 误传整条路径时也不会得到意外的结果。
            qsizetype limit = ni;
            while (limit < name.size() && name.at(limit) != u'/') {
                ++limit;
            }
            for (qsizetype k = ni; k <= limit; ++k) {
                if (match_here(pattern, pi, name, k, cs)) {
                    return true;
                }
            }
            return false;
        }

        if (ni >= name.size()) {
            return false;
        }

        if (pc == u'?') {
            if (name.at(ni) == u'/') {
                return false;
            }
            ++pi;
            ++ni;
            continue;
        }

        if (pc == u'[') {
            qsizetype after = 0;
            const bool matched = match_class(pattern, pi, name.at(ni), &after, cs);
            if (after == 0) {
                // 不是合法字符集 —— 按字面 `[` 处理
                if (!char_equal(pc, name.at(ni), cs)) {
                    return false;
                }
                ++pi;
                ++ni;
                continue;
            }
            if (!matched) {
                return false;
            }
            pi = after;
            ++ni;
            continue;
        }

        if (pc == u'\\' && pi + 1 < pattern.size()) {
            ++pi;
        }
        if (!char_equal(pattern.at(pi), name.at(ni), cs)) {
            return false;
        }
        ++pi;
        ++ni;
    }

    return ni == name.size();
}

/// 把 `filter` 拆成多个 glob（`;` 或 `,` 分隔）。空串表示不过滤。
QStringList split_patterns(const QString& filter) {
    QStringList patterns;
    for (const QString& piece :
         filter.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts)) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty()) {
            patterns.append(trimmed);
        }
    }
    return patterns;
}

bool name_matches(const QStringList& patterns, const QString& name) {
    if (patterns.isEmpty()) {
        return true;
    }
    return std::any_of(patterns.cbegin(), patterns.cend(), [&name](const QString& pattern) {
        return glob_match(pattern, name);
    });
}

/// 递归层数上限。目录符号链接已由 `QDir::NoSymLinks` 挡住，这里是最后一道保险
/// （Windows 的 junction / 挂载点在某些情况下也成环）。
constexpr int kMaxDepth = 64;

void collect(const QDir& dir,
             const QString& prefix,
             const DirQuery& query,
             const QStringList& patterns,
             int depth,
             QStringList* out) {
    if (depth > kMaxDepth) {
        return;
    }

    // QDir::Hidden 让隐藏项**先被列出**，再由我们自己的规则筛 ——
    // 这样"什么算隐藏"在三平台上只有一个答案（见 DirQuery::include_hidden 注释）。
    constexpr QDir::Filters kBaseFilters = QDir::NoDotAndDotDot | QDir::Hidden;

    if (query.include_dirs || query.recursive) {
        // 符号链接目录一律不进入：它可能指回上层，无限递归下去。
        const QFileInfoList subdirs =
                dir.entryInfoList(QDir::Dirs | kBaseFilters | QDir::NoSymLinks, QDir::NoSort);
        for (const QFileInfo& subdir : subdirs) {
            if (!query.include_hidden && subdir.fileName().startsWith(u'.')) {
                continue;
            }
            const QString relative = prefix + subdir.fileName();
            // 过滤只决定"这个目录要不要作为条目出现"，**不决定要不要进去** ——
            // 否则 `filter=*.mkv` 会因为目录名不匹配而整棵子树都不扫，
            // 于是 `sub/c.mkv` 这类明明该出现的结果静默消失。
            if (query.include_dirs && name_matches(patterns, subdir.fileName())) {
                out->append(relative);
            }
            if (query.recursive) {
                collect(QDir(subdir.absoluteFilePath()),
                        relative + QLatin1Char('/'),
                        query,
                        patterns,
                        depth + 1,
                        out);
            }
        }
    }

    const QFileInfoList files = dir.entryInfoList(QDir::Files | kBaseFilters, QDir::NoSort);
    for (const QFileInfo& file : files) {
        if (!query.include_hidden && file.fileName().startsWith(u'.')) {
            continue;
        }
        if (!name_matches(patterns, file.fileName())) {
            continue;
        }
        out->append(prefix + file.fileName());
    }
}

}  // namespace

bool glob_match(QStringView pattern, QStringView name, Qt::CaseSensitivity cs) {
    // 空模式只匹配空名字。注意这与"filter 为空表示不过滤"是两回事：
    // 那个判断在名字匹配之前就做了（见 name_matches），不会走到这里。
    return match_here(pattern, 0, name, 0, cs);
}

DirScan scan_directory(const DirQuery& query) {
    DirScan result;

    if (query.path.trimmed().isEmpty()) {
        result.error = QStringLiteral("列表绑定的文件夹路径为空");
        return result;
    }

    const QFileInfo info(query.path);
    if (!info.exists()) {
        result.error = QStringLiteral("文件夹不存在：%1").arg(QDir::toNativeSeparators(query.path));
        return result;
    }
    if (!info.isDir()) {
        result.error =
                QStringLiteral("不是一个文件夹：%1").arg(QDir::toNativeSeparators(query.path));
        return result;
    }

    const QDir root(info.absoluteFilePath());
    if (!root.isReadable()) {
        result.error = QStringLiteral("文件夹不可读：%1").arg(QDir::toNativeSeparators(query.path));
        return result;
    }

    collect(root, QString(), query, split_patterns(query.filter), 0, &result.items);

    // 自然序 —— 这是正确性要求，不是体验优化（见头文件里的说明）。
    std::sort(result.items.begin(), result.items.end(), [](const QString& a, const QString& b) {
        return natural_less(QStringView(a), QStringView(b));
    });

    return result;
}

}  // namespace batchsmith::core
