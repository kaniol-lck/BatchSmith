#pragma once

#include <QString>
#include <QStringView>
#include <Qt>

namespace batchsmith::core {

/// 自然排序比较：把连续数字当作数值比较，因此 `file2` 排在 `file10` 之前。
///
/// 这是一条**正确性**要求而不是体验优化：列表源绑定文件夹后，默认排序若按
/// 字典序，`file10` 会排在 `file2` 前面；而"文件行绑定绝对路径"的安全模型
/// 依赖用户能凭排序直观核对结果（见技术方案 §4.4 / §4.6）。
///
/// 规则：
///   * 数字段按数值比较（跳过前导零后先比位数、再比逐位）
///   * 数值相等时（如 "01" 与 "1"）前导零少的排前面，保证全序
///   * 数字段整体排在非数字段之前
///   * 非数字段逐码元比较，`cs` 控制是否忽略大小写
///
/// 返回值语义与 `QString::compare` 一致：负数表示 a<b，0 表示相等，正数表示 a>b。
///
/// @note 非数字段默认按 UTF-16 码元序（确定性、三平台一致、便于测试）。
///       若要中文按拼音排序，需要接入 `QLocale` 的 collation，属于后续增强，
///       不要直接替换本函数的默认行为——那会让排序结果随系统区域设置漂移。
[[nodiscard]] int natural_compare(QStringView a,
                                  QStringView b,
                                  Qt::CaseSensitivity cs = Qt::CaseSensitive) noexcept;

/// 供 `std::sort` 等直接使用的便捷谓词。
[[nodiscard]] inline bool natural_less(QStringView a,
                                       QStringView b,
                                       Qt::CaseSensitivity cs = Qt::CaseSensitive) noexcept {
    return natural_compare(a, b, cs) < 0;
}

}  // namespace batchsmith::core
