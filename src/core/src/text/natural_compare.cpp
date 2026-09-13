#include "batchsmith/core/text/natural_compare.hpp"

namespace batchsmith::core {
namespace {

constexpr bool is_digit(QChar c) noexcept {
    return c >= u'0' && c <= u'9';
}

/// 按需折叠大小写。中文等无大小写概念的字符会原样返回。
constexpr char16_t fold(QChar c, Qt::CaseSensitivity cs) noexcept {
    return cs == Qt::CaseInsensitive ? QChar(c).toCaseFolded().unicode() : c.unicode();
}

/// 跳过数字段的前导零，但至少保留一位（"000" 会停在最后一个 '0'）。
[[nodiscard]] qsizetype skip_leading_zeros(QStringView s, qsizetype begin, qsizetype end) noexcept {
    while (begin < end - 1 && s.at(begin) == u'0') {
        ++begin;
    }
    return begin;
}

/// 比较两个已跳过前导零的数字段。
/// 位数多的数值一定更大，所以先比有效位数，位数相同再逐位比。
[[nodiscard]] int compare_digit_run(QStringView a,
                                    qsizetype a0,
                                    qsizetype a1,
                                    QStringView b,
                                    qsizetype b0,
                                    qsizetype b1) noexcept {
    const qsizetype len_a = a1 - a0;
    const qsizetype len_b = b1 - b0;

    if (len_a != len_b) {
        return len_a < len_b ? -1 : 1;
    }

    for (qsizetype i = 0; i < len_a; ++i) {
        const char16_t ca = a.at(a0 + i).unicode();
        const char16_t cb = b.at(b0 + i).unicode();
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }

    return 0;
}

}  // namespace

int natural_compare(QStringView a, QStringView b, Qt::CaseSensitivity cs) noexcept {
    qsizetype i = 0;
    qsizetype j = 0;
    const qsizetype size_a = a.size();
    const qsizetype size_b = b.size();

    while (i < size_a && j < size_b) {
        const bool digit_a = is_digit(a.at(i));
        const bool digit_b = is_digit(b.at(j));

        if (digit_a && digit_b) {
            qsizetype end_a = i;
            while (end_a < size_a && is_digit(a.at(end_a))) {
                ++end_a;
            }
            qsizetype end_b = j;
            while (end_b < size_b && is_digit(b.at(end_b))) {
                ++end_b;
            }

            const qsizetype start_a = skip_leading_zeros(a, i, end_a);
            const qsizetype start_b = skip_leading_zeros(b, j, end_b);

            if (const int result = compare_digit_run(a, start_a, end_a, b, start_b, end_b);
                result != 0) {
                return result;
            }

            // 数值相等（例如 "01" 与 "1"）时用前导零个数兜底。
            // 没有这一步，"01x" 与 "1x" 会被判为相等，破坏排序的全序性。
            const qsizetype zeros_a = start_a - i;
            const qsizetype zeros_b = start_b - j;
            if (zeros_a != zeros_b) {
                return zeros_a < zeros_b ? -1 : 1;
            }

            i = end_a;
            j = end_b;
            continue;
        }

        if (digit_a != digit_b) {
            // 数字段排在非数字段之前，与多数文件管理器一致
            return digit_a ? -1 : 1;
        }

        const char16_t ca = fold(a.at(i), cs);
        const char16_t cb = fold(b.at(j), cs);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }

        ++i;
        ++j;
    }

    // 一方跑完时，更长的那个排在后面
    if (i < size_a) {
        return 1;
    }
    if (j < size_b) {
        return -1;
    }
    return 0;
}

}  // namespace batchsmith::core
