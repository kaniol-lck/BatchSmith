#include "batchsmith/core/preset/preset.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringView>
#include <QtGlobal>

#include <toml++/toml.hpp>

namespace batchsmith::core {
namespace {

// ---------------------------------------------------------------------------
// 槽位
// ---------------------------------------------------------------------------

/// 槽位名允许的字符：`[A-Za-z0-9_]`。
/// 刻意收窄 —— 槽位会被写进 TOML、也会在界面里当标签用，允许奇怪字符只会带来麻烦。
[[nodiscard]] bool is_slot_char(QChar ch) {
    return (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') || (ch >= u'0' && ch <= u'9') ||
           ch == u'_';
}

// ---------------------------------------------------------------------------
// TOML 文本生成
// ---------------------------------------------------------------------------

/// 把一个字符串写成 TOML 字符串。
///
/// 优先用**字面字符串**（单引号）：路径里的反斜杠、模板里的 `$` 与 `\`、正则里的 `\d`
/// 全都不用转义，人读起来就是原样 —— 预设文件是要给人看、给人改的。
/// 含单引号或换行时才退回基本字符串并逐字符转义。
[[nodiscard]] QString toml_string(const QString& text) {
    const bool needs_escape = text.contains(QLatin1Char('\'')) ||
                              text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'));
    if (!needs_escape) {
        return QStringLiteral("'") + text + QStringLiteral("'");
    }

    QString out = QStringLiteral("\"");
    for (const QChar ch : text) {
        switch (ch.unicode()) {
            case u'"':
                out += QLatin1String("\\\"");
                break;
            case u'\\':
                out += QLatin1String("\\\\");
                break;
            case u'\n':
                out += QLatin1String("\\n");
                break;
            case u'\r':
                out += QLatin1String("\\r");
                break;
            case u'\t':
                out += QLatin1String("\\t");
                break;
            default:
                if (ch.unicode() < 0x20) {
                    out += QStringLiteral("\\u%1").arg(ch.unicode(), 4, 16, QLatin1Char('0'));
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out + QStringLiteral("\"");
}

[[nodiscard]] QString toml_string_array(const QStringList& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (const QString& value : values) {
        parts.append(toml_string(value));
    }
    return QStringLiteral("[") + parts.join(QStringLiteral(", ")) + QStringLiteral("]");
}

[[nodiscard]] QString fill_to_text(ListPadding padding) {
    switch (padding) {
        case ListPadding::Empty:
            return QStringLiteral("empty");
        case ListPadding::Ignore:
            return QStringLiteral("ignore");
        case ListPadding::Repeat:
            return QStringLiteral("repeat");
    }
    return QStringLiteral("empty");
}

// ---------------------------------------------------------------------------
// TOML 读取的小工具
// ---------------------------------------------------------------------------

[[nodiscard]] QString node_kind_name(const toml::node& node) {
    if (node.is_string()) {
        return QStringLiteral("字符串");
    }
    if (node.is_boolean()) {
        return QStringLiteral("布尔值");
    }
    if (node.is_integer()) {
        return QStringLiteral("整数");
    }
    if (node.is_array()) {
        return QStringLiteral("数组");
    }
    if (node.is_table()) {
        return QStringLiteral("表");
    }
    return QStringLiteral("其它类型");
}

/// 取字符串字段。字段不存在返回空；**存在但类型不对**时报错 ——
/// 静默忽略类型错误会让"预设看起来加载成功了，实际少了半截设置"。
[[nodiscard]] bool read_string(const toml::table& table,
                               std::string_view key,
                               QString* out,
                               QString* error) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return true;
    }
    const auto* text = node->as_string();
    if (text == nullptr) {
        *error = QStringLiteral("%1 应该是字符串，实际是%2")
                         .arg(QString::fromUtf8(key.data(), static_cast<int>(key.size())),
                              node_kind_name(*node));
        return false;
    }
    *out = QString::fromStdString(text->get());
    return true;
}

[[nodiscard]] bool read_bool(
        const toml::table& table, std::string_view key, bool fallback, bool* out, QString* error) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        *out = fallback;
        return true;
    }
    const auto* value = node->as_boolean();
    if (value == nullptr) {
        *error = QStringLiteral("%1 应该是 true / false，实际是%2")
                         .arg(QString::fromUtf8(key.data(), static_cast<int>(key.size())),
                              node_kind_name(*node));
        return false;
    }
    *out = value->get();
    return true;
}

[[nodiscard]] bool read_string_array(const toml::table& table,
                                     std::string_view key,
                                     QStringList* out,
                                     QString* error) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return true;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        *error = QStringLiteral("%1 应该是数组，实际是%2")
                         .arg(QString::fromUtf8(key.data(), static_cast<int>(key.size())),
                              node_kind_name(*node));
        return false;
    }
    QStringList values;
    for (const auto& item : *array) {
        const auto* text = item.as_string();
        if (text == nullptr) {
            *error = QStringLiteral("%1 里有一项不是字符串")
                             .arg(QString::fromUtf8(key.data(), static_cast<int>(key.size())));
            return false;
        }
        values.append(QString::fromStdString(text->get()));
    }
    *out = values;
    return true;
}

[[nodiscard]] ListPadding fill_from_text(const QString& text) {
    if (text == QLatin1String("ignore")) {
        return ListPadding::Ignore;
    }
    if (text == QLatin1String("repeat")) {
        return ListPadding::Repeat;
    }
    return ListPadding::Empty;
}

[[nodiscard]] bool read_whole_file(const QString& path, QString* out, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("打不开文件：%1（%2）")
                         .arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    *out = QString::fromUtf8(file.readAll());
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 槽位
// ---------------------------------------------------------------------------

QStringList slots_in(QStringView text) {
    // 注意：变量不能叫 `slots` —— 那是 Qt 的宏（Q_SLOTS），会被预处理器吃掉，
    // 症状是一堆莫名其妙的 "declaration does not declare anything"。
    QStringList names;
    qsizetype index = 0;
    while (index < text.size()) {
        const qsizetype start = text.indexOf(QLatin1String("${"), index);
        if (start < 0) {
            break;
        }
        const qsizetype end = text.indexOf(QLatin1Char('}'), start + 2);
        if (end < 0) {
            break;
        }
        const QStringView name = text.sliced(start + 2, end - start - 2);
        bool valid = !name.isEmpty();
        for (const QChar ch : name) {
            if (!is_slot_char(ch)) {
                valid = false;
                break;
            }
        }
        if (valid && !names.contains(name.toString())) {
            names.append(name.toString());
        }
        index = end + 1;
    }
    return names;
}

QStringList preset_slots(const Preset& preset) {
    QStringList names;
    for (const PresetList& list : preset.lists) {
        for (const QString& slot : slots_in(list.dir.path)) {
            if (!names.contains(slot)) {
                names.append(slot);
            }
        }
    }
    return names;
}

QString bind_slots(QStringView text, const SlotBindings& bindings) {
    QString result;
    result.reserve(text.size());

    qsizetype index = 0;
    while (index < text.size()) {
        const qsizetype start = text.indexOf(QLatin1String("${"), index);
        if (start < 0) {
            result += text.sliced(index).toString();
            break;
        }
        const qsizetype end = text.indexOf(QLatin1Char('}'), start + 2);
        if (end < 0) {
            result += text.sliced(index).toString();
            break;
        }

        const QString name = text.sliced(start + 2, end - start - 2).toString();
        result += text.sliced(index, start - index).toString();

        const auto it = bindings.constFind(name);
        if (it != bindings.cend()) {
            result += it.value();
        } else {
            // 未绑定：原样留着，让它在下游以「文件夹不存在：${input}」的形式暴露出来
            result += text.sliced(start, end - start + 1).toString();
        }
        index = end + 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------

QString preset_to_toml(const Preset& preset) {
    QStringList lines;

    lines.append(
            QStringLiteral("# BatchSmith 预设 —— 由界面「文件 → 保存预设」写出，也可以手改。"));
    lines.append(QStringLiteral("#"));
    lines.append(
            QStringLiteral("# 文件夹路径里的 ${input} 是**运行时槽位**：预设里不存本机绝对路径，"));
    lines.append(
            QStringLiteral("# 于是这个文件可以进版本控制、也可以发给别人。本机绑定的实际路径存在"));
    lines.append(QStringLiteral("# 旁边的 <文件名>.local.toml 里 —— 分享时不要带那个文件。"));
    lines.append(QStringLiteral("#"));
    lines.append(QStringLiteral("# 模板语法与全部工具函数见「帮助 → DSL 语法与函数速查」（F1）。"));
    lines.append(QString());

    lines.append(QStringLiteral("[preset]"));
    lines.append(QStringLiteral("name = %1").arg(toml_string(preset.name)));
    lines.append(QStringLiteral("version = %1").arg(preset.version));
    lines.append(QStringLiteral("level = %1").arg(toml_string(preset.level)));
    // 备注只在非空时写出：空备注写一行 `note = ''` 只是噪音，
    // 而"没写 note"与"note 是空串"在读侧是同一件事
    if (!preset.note.isEmpty()) {
        lines.append(QStringLiteral("note = %1").arg(toml_string(preset.note)));
    }
    lines.append(QString());

    for (const PresetList& list : preset.lists) {
        lines.append(QStringLiteral("[[lists]]"));
        lines.append(QStringLiteral("id = %1").arg(toml_string(list.id)));

        if (list.kind == ListSourceKind::Manual) {
            // 手输列表不写 source：items 本身就说明了来源，少一层没必要
            lines.append(QStringLiteral("items = %1").arg(toml_string_array(list.items)));
        } else {
            // `source` 用 TOML 的 inline table（技术方案 ADR-7 的写法）：
            // 把「从哪取数」与「长度不齐时怎么办（fill）」在文件里分开，
            // 这两件事本来就不是一回事。
            QStringList fields;
            fields.append(QStringLiteral("kind = 'dir'"));
            fields.append(QStringLiteral("path = %1").arg(toml_string(list.dir.path)));
            if (!list.dir.filter.isEmpty()) {
                fields.append(QStringLiteral("filter = %1").arg(toml_string(list.dir.filter)));
            }
            fields.append(QStringLiteral("recursive = %1")
                                  .arg(list.dir.recursive ? QStringLiteral("true")
                                                          : QStringLiteral("false")));
            fields.append(QStringLiteral("dirs = %1")
                                  .arg(list.dir.include_dirs ? QStringLiteral("true")
                                                             : QStringLiteral("false")));
            fields.append(QStringLiteral("hidden = %1")
                                  .arg(list.dir.include_hidden ? QStringLiteral("true")
                                                               : QStringLiteral("false")));
            lines.append(QStringLiteral("source = { %1 }").arg(fields.join(QStringLiteral(", "))));
        }
        // 注意：枚举要**加引号**再写出去，否则 `fill = empty` 不是合法 TOML
        // （往返测试当场抓到了这一处）
        lines.append(QStringLiteral("fill = %1").arg(toml_string(fill_to_text(list.fill))));
        lines.append(QString());
    }

    lines.append(QStringLiteral("[output]"));
    lines.append(QStringLiteral("mode = %1").arg(toml_string(preset.output_mode)));
    lines.append(QStringLiteral("template = %1").arg(toml_string(preset.template_text)));
    lines.append(QString());

    return lines.join(QLatin1Char('\n'));
}

PresetLoad preset_from_toml(const QString& text, const QString& source_name) {
    PresetLoad result;

    // 这两个 std::string 必须活到 parse 结束：toml++ 的 source_path 是 string_view，
    // 传临时对象的话指向的就是已释放的内存。
    const std::string text_utf8 = text.toStdString();
    const std::string name_utf8 =
            source_name.isEmpty() ? std::string{"<预设>"} : source_name.toStdString();

    toml::table root;
    try {
        root = toml::parse(text_utf8, name_utf8);
    } catch (const toml::parse_error& error) {
        const auto& position = error.source().begin;
        result.error = QStringLiteral("不是合法的 TOML：%1（第 %2 行第 %3 列）")
                               .arg(QString::fromStdString(std::string(error.description())))
                               .arg(position.line)
                               .arg(position.column);
        return result;
    }

    const toml::table* preset_table = root["preset"].as_table();
    if (preset_table == nullptr) {
        result.error = QStringLiteral("缺少 [preset] 段");
        return result;
    }

    Preset preset;
    QString field_error;

    if (!read_string(*preset_table, "name", &preset.name, &field_error)) {
        result.error = QStringLiteral("[preset] %1").arg(field_error);
        return result;
    }
    if (!read_string(*preset_table, "level", &preset.level, &field_error)) {
        result.error = QStringLiteral("[preset] %1").arg(field_error);
        return result;
    }
    // 备注是可选字段（老预设里没有），但要校验类型：写成数组/表时应当报错，
    // 而不是被静默当成空备注
    if (!read_string(*preset_table, "note", &preset.note, &field_error)) {
        result.error = QStringLiteral("[preset] %1").arg(field_error);
        return result;
    }

    if (const toml::node* version_node = preset_table->get("version")) {
        const auto* version = version_node->as_integer();
        if (version == nullptr) {
            result.error = QStringLiteral("[preset] version 应该是整数，实际是%1")
                                   .arg(node_kind_name(*version_node));
            return result;
        }
        const auto value = version->get();
        if (value > kPresetVersion) {
            // 高版本文件不猜着读：猜错了会静默产出错误的批量操作
            result.error =
                    QStringLiteral("这个预设是更新版本写的（version = %1，本版本支持到 %2），"
                                   "请升级 BatchSmith 再打开")
                            .arg(value)
                            .arg(kPresetVersion);
            return result;
        }
        preset.version = static_cast<int>(value);
    }

    if (const toml::node* lists_node = root.get("lists")) {
        const auto* lists = lists_node->as_array();
        if (lists == nullptr) {
            result.error = QStringLiteral("lists 应该是数组（每项一个 [[lists]] 段）");
            return result;
        }

        for (const auto& item : *lists) {
            const toml::table* table = item.as_table();
            if (table == nullptr) {
                result.error = QStringLiteral("[[lists]] 里有一项不是表");
                return result;
            }

            PresetList list;
            if (!read_string(*table, "id", &list.id, &field_error)) {
                result.error = QStringLiteral("[[lists]] %1").arg(field_error);
                return result;
            }
            if (list.id.isEmpty()) {
                // id 就是表达式里的列表名（list1、list2…）。缺它就只能靠猜，
                // 而猜错了表达式会引用到不存在的名字 —— 与其猜，不如让他写清楚。
                result.error =
                        QStringLiteral("[[lists]] 缺少 id（表达式里靠它引用，如 id = 'list1'）");
                return result;
            }

            if (!read_string_array(*table, "items", &list.items, &field_error)) {
                result.error = QStringLiteral("[[lists]] %1").arg(field_error);
                return result;
            }

            // `source` 用 inline table（技术方案 ADR-7 的写法）：
            //   source = { kind = 'dir', path = '${input}', filter = '*.mkv' }
            // 缺失时按手输处理 —— items 本身就是内容，不必再声明一次来源。
            if (const toml::node* source_node = table->get("source")) {
                const toml::table* source = source_node->as_table();
                if (source == nullptr) {
                    result.error =
                            QStringLiteral("[[lists]] %1 的 source 要写成表，"
                                           "例如 source = { kind = 'dir', path = '${input}' }")
                                    .arg(list.id);
                    return result;
                }

                QString kind_text;
                if (!read_string(*source, "kind", &kind_text, &field_error)) {
                    result.error =
                            QStringLiteral("[[lists]] %1 source.%2").arg(list.id, field_error);
                    return result;
                }

                if (kind_text.isEmpty() || kind_text == QLatin1String("manual")) {
                    list.kind = ListSourceKind::Manual;
                } else if (kind_text == QLatin1String("dir")) {
                    list.kind = ListSourceKind::Directory;

                    if (!read_string(*source, "path", &list.dir.path, &field_error) ||
                        !read_string(*source, "filter", &list.dir.filter, &field_error) ||
                        !read_bool(
                                *source, "recursive", false, &list.dir.recursive, &field_error) ||
                        !read_bool(*source, "dirs", false, &list.dir.include_dirs, &field_error) ||
                        !read_bool(
                                *source, "hidden", false, &list.dir.include_hidden, &field_error)) {
                        result.error =
                                QStringLiteral("[[lists]] %1 source.%2").arg(list.id, field_error);
                        return result;
                    }

                    // 排序固定自然序。读到别的值要报错 —— 静默忽略会让用户以为
                    // "按修改时间排"生效了，而实际结果仍是自然序。
                    QString sort_text;
                    if (!read_string(*source, "sort", &sort_text, &field_error)) {
                        result.error =
                                QStringLiteral("[[lists]] %1 source.%2").arg(list.id, field_error);
                        return result;
                    }
                    if (!sort_text.isEmpty() && sort_text != QLatin1String("natural")) {
                        result.error =
                                QStringLiteral("[[lists]] %1 的 source.sort = '%2' 不支持："
                                               "条目固定按自然序排列（file2 在 file10 之前），"
                                               "没有其它可选项")
                                        .arg(list.id, sort_text);
                        return result;
                    }
                } else {
                    result.error =
                            QStringLiteral("[[lists]] %1 的 source.kind = '%2' 本版本还不支持"
                                           "（当前支持 'manual' 与 'dir'）")
                                    .arg(list.id, kind_text);
                    return result;
                }
            }

            QString fill_text;
            if (!read_string(*table, "fill", &fill_text, &field_error)) {
                result.error = QStringLiteral("[[lists]] %1").arg(field_error);
                return result;
            }
            if (!fill_text.isEmpty() && fill_text != QLatin1String("empty") &&
                fill_text != QLatin1String("ignore") && fill_text != QLatin1String("repeat")) {
                result.error =
                        QStringLiteral("[[lists]] fill 只支持 'empty' / 'ignore' / 'repeat'，"
                                       "收到 '%1'")
                                .arg(fill_text);
                return result;
            }
            list.fill = fill_from_text(fill_text);

            preset.lists.append(list);
        }
    }

    if (const toml::table* output = root["output"].as_table()) {
        if (!read_string(*output, "template", &preset.template_text, &field_error)) {
            result.error = QStringLiteral("[output] %1").arg(field_error);
            return result;
        }
        if (!read_string(*output, "mode", &preset.output_mode, &field_error)) {
            result.error = QStringLiteral("[output] %1").arg(field_error);
            return result;
        }
        // 现在就校验、只是还没实现执行（Phase 4）：
        // 让 `mode = "argv"` 当场报错，好过被静默忽略后让人以为配了就能用
        if (preset.output_mode != QLatin1String("rename")) {
            result.error =
                    QStringLiteral("[output] mode = '%1' 本版本还不支持（当前只支持 'rename'）")
                            .arg(preset.output_mode);
            return result;
        }
    }

    // 未知字段**刻意忽略**：以后版本往预设里加字段时，旧版本仍应能打开它 ——
    // 版本号已经在前面挡下了"结构性变化"那种情况。
    result.preset = preset;
    return result;
}

// ---------------------------------------------------------------------------
// 文件
// ---------------------------------------------------------------------------

QString bindings_path_for(const QString& preset_path) {
    const QFileInfo info(preset_path);
    const QString base = info.completeBaseName();  // foo.toml → foo（多重后缀只去掉最后一段）
    const QString suffix = info.suffix();
    const QString file_name = suffix.isEmpty() ? base + QStringLiteral(".local")
                                               : base + QStringLiteral(".local.") + suffix;
    return info.absoluteDir().filePath(file_name);
}

QString default_preset_directory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("presets"));
}

bool ensure_preset_directory(QString* error) {
    const QString path = default_preset_directory();
    if (QDir().mkpath(path)) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("建不了预设目录：%1").arg(QDir::toNativeSeparators(path));
    }
    return false;
}

QStringList list_preset_files() {
    QDir directory(default_preset_directory());
    if (!directory.exists()) {
        return {};
    }
    const QFileInfoList entries =
            directory.entryInfoList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name);
    QStringList files;
    files.reserve(entries.size());
    for (const QFileInfo& entry : entries) {
        // `.local.toml` 是本机绑定，不是预设 —— 别把它列出来让用户以为是个预设
        if (entry.completeBaseName().endsWith(QLatin1String(".local"))) {
            continue;
        }
        files.append(entry.absoluteFilePath());
    }
    return files;
}

namespace {

/// 把用户给的名字净化成"能当文件名用"的形式。
///
/// 不做这一步的话，一个叫 `番剧/第2季` 的预设会尝试往子目录里写文件（失败），
/// 一个叫 `a:b` 的在 Windows 上直接失败 —— 而这类失败发生在"保存"这个动作里，
/// 用户看到的只是"存不上"。
[[nodiscard]] QString sanitize_file_base(QString name) {
    static const QString kIllegal = QStringLiteral("<>:\"/\\|?*");

    name = name.trimmed();
    for (QChar& ch : name) {
        // 控制字符在文件名里是未定义行为（Windows 直接拒绝）
        if (ch.unicode() < 0x20 || kIllegal.contains(ch)) {
            ch = QLatin1Char('_');
        }
    }

    // Windows 上文件名不能以点或空格结尾（会静默被去掉，于是"存了但名字不对"）
    while (!name.isEmpty() &&
           (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))) {
        name.chop(1);
    }

    // `.local` 是绑定文件的后缀，用它当预设名会导致下次读不出来
    while (name.endsWith(QLatin1String(".local"), Qt::CaseInsensitive)) {
        name.chop(6);
        while (!name.isEmpty() &&
               (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))) {
            name.chop(1);
        }
    }

    return name;
}

}  // namespace

QString unique_file_path(const QString& directory,
                         const QString& base_name,
                         const QString& suffix,
                         const QString& fallback) {
    QString base = sanitize_file_base(base_name);
    if (base.isEmpty()) {
        base = sanitize_file_base(fallback);
    }
    if (base.isEmpty()) {
        base = QStringLiteral("未命名");
    }

    const QDir dir(directory);
    const auto path_for = [&dir, &suffix](const QString& stem) {
        return dir.filePath(stem + suffix);
    };

    if (!QFileInfo::exists(path_for(base))) {
        return path_for(base);
    }
    // 从 2 开始试：`名字 2.toml` 比 `名字 (2).toml` 更像人手打的，也不含容易
    // 被各种工具转义的括号。999 之后放弃计数、直接盖在最后一个上 ——
    // 真到那一步说明用户在批量生成，给个能用的路径比报错好。
    for (int index = 2; index <= 999; ++index) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(index);
        if (!QFileInfo::exists(path_for(candidate))) {
            return path_for(candidate);
        }
    }
    return path_for(QStringLiteral("%1 999").arg(base));
}

bool save_preset(const Preset& preset, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("写不了文件：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }

    const QByteArray bytes = preset_to_toml(preset).toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("写入不完整：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    file.close();

    if (file.error() != QFileDevice::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("保存失败：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    return true;
}

PresetLoad load_preset(const QString& path) {
    PresetLoad result;

    QString text;
    if (!read_whole_file(path, &text, &result.error)) {
        return result;
    }

    result = preset_from_toml(text, QFileInfo(path).fileName());
    if (result.ok()) {
        result.preset.file_path = path;
        // 文件里没写名字（手写的预设很常见）时，用文件名兜底 ——
        // 标题栏与「最近打开」总得有个能认的名字
        if (result.preset.name.isEmpty()) {
            result.preset.name = QFileInfo(path).completeBaseName();
        }
    }
    return result;
}

bool save_local_settings(const LocalSettings& settings,
                         const QString& preset_path,
                         QString* error) {
    QStringList lines;
    lines.append(QStringLiteral("# 本机设置 —— 上面那个预设在这台机器上的东西。"));
    lines.append(QStringLiteral("#"));
    lines.append(
            QStringLiteral("# 这里全是**本机信息**（路径、图标），分享预设时请不要带这个文件。"));
    lines.append(QStringLiteral("# 删掉它不会损坏预设，只是下次加载时需要重新绑定路径。"));
    lines.append(QString());
    lines.append(QStringLiteral("[bindings]"));

    // 排序输出：同一份设置每次写出的文件都一样，便于 diff 与版本控制
    QStringList keys = settings.bindings.keys();
    keys.sort();
    for (const QString& key : keys) {
        lines.append(QStringLiteral("%1 = %2").arg(key, toml_string(settings.bindings.value(key))));
    }

    if (!settings.shortcut_icon.isEmpty()) {
        lines.append(QString());
        lines.append(
                QStringLiteral("# 「创建快捷方式」时给快捷方式用的图标（不设就用程序自带图标）"));
        lines.append(QStringLiteral("[shortcut]"));
        lines.append(QStringLiteral("icon = %1").arg(toml_string(settings.shortcut_icon)));
    }
    lines.append(QString());

    QFile file(bindings_path_for(preset_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("写不了本机设置文件：%1（%2）")
                             .arg(QDir::toNativeSeparators(file.fileName()), file.errorString());
        }
        return false;
    }
    const QByteArray bytes = lines.join(QLatin1Char('\n')).toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("本机设置文件写入不完整：%1").arg(file.fileName());
        }
        return false;
    }
    file.close();
    return true;
}

LocalSettings load_local_settings(const QString& preset_path, QString* error) {
    LocalSettings settings;

    const QString path = bindings_path_for(preset_path);
    if (!QFileInfo::exists(path)) {
        return settings;  // 还没绑过，不算错
    }

    QString text;
    QString read_error;
    if (!read_whole_file(path, &text, &read_error)) {
        if (error != nullptr) {
            *error = read_error;
        }
        return settings;
    }

    toml::table root;
    const std::string text_utf8 = text.toStdString();
    const std::string path_utf8 = path.toStdString();
    try {
        root = toml::parse(text_utf8, path_utf8);
    } catch (const toml::parse_error& parse_error) {
        if (error != nullptr) {
            *error = QStringLiteral("本机设置文件不是合法的 TOML：%1（%2）")
                             .arg(QString::fromStdString(std::string(parse_error.description())),
                                  QDir::toNativeSeparators(path));
        }
        return settings;
    }

    if (const toml::table* table = root["bindings"].as_table()) {
        for (const auto& [key, value] : *table) {
            const std::string key_utf8{key.str()};
            const auto* text_value = value.as_string();
            if (text_value == nullptr) {
                if (error != nullptr) {
                    *error = QStringLiteral("本机设置文件里的 %1 不是字符串")
                                     .arg(QString::fromStdString(key_utf8));
                }
                return {};
            }
            settings.bindings.insert(QString::fromStdString(key_utf8),
                                     QString::fromStdString(text_value->get()));
        }
    }

    if (const toml::table* shortcut = root["shortcut"].as_table()) {
        QString field_error;
        if (!read_string(*shortcut, "icon", &settings.shortcut_icon, &field_error)) {
            if (error != nullptr) {
                *error = QStringLiteral("[shortcut] %1").arg(field_error);
            }
            return {};
        }
    }
    return settings;
}

bool save_bindings(const SlotBindings& bindings, const QString& preset_path, QString* error) {
    // 先读回已有的本机设置，只换掉绑定那一节 —— 直接拿一份"只有绑定"的结构去覆盖
    // 整个文件，会把图标设置抹掉，而用户完全不会把这两件事联系起来
    LocalSettings settings = load_local_settings(preset_path);
    settings.bindings = bindings;
    return save_local_settings(settings, preset_path, error);
}

SlotBindings load_bindings(const QString& preset_path, QString* error) {
    return load_local_settings(preset_path, error).bindings;
}

// ---------------------------------------------------------------------------
// 与运行时表示的互转
// ---------------------------------------------------------------------------

ListSourceList preset_sources(const Preset& preset,
                              const SlotBindings& bindings,
                              QHash<QString, QString>* errors) {
    ListSourceList sources;
    sources.reserve(preset.lists.size());

    for (const PresetList& item : preset.lists) {
        ListSource source;
        source.name = item.id;
        source.kind = item.kind;
        source.padding = item.fill;

        if (item.kind == ListSourceKind::Manual) {
            source.items = item.items;
        } else {
            source.dir = item.dir;
            source.dir.path = bind_slots(item.dir.path, bindings);

            QString scan_error;
            if (!refresh(source, &scan_error) && errors != nullptr) {
                // 单列读不到不该让整个预设打不开 —— 用户往往只是想改个路径
                errors->insert(item.id, scan_error);
            }
        }
        sources.append(source);
    }
    return sources;
}

Preset preset_from_sources(const ListSourceList& sources,
                           QString template_text,
                           const QString& name,
                           SlotBindings* bindings) {
    Preset preset;
    preset.name = name;
    preset.template_text = std::move(template_text);

    // 路径 → 槽位名。**同一个路径共用一个槽位**：两个列表绑同一个文件夹、
    // 只是过滤不同是很常见的写法，这时文件里只该出现一次路径（改绑定也只改一处）。
    QHash<QString, QString> slot_of_path;
    int slot_count = 0;

    for (const ListSource& source : sources) {
        PresetList item;
        item.id = source.name;
        item.kind = source.kind;
        item.fill = source.padding;

        if (source.kind == ListSourceKind::Manual) {
            item.items = source.items;
        } else {
            item.dir = source.dir;
            const QString path = source.dir.path;
            if (!path.isEmpty()) {
                QString slot = slot_of_path.value(path);
                if (slot.isEmpty()) {
                    ++slot_count;
                    slot = (slot_count == 1) ? QStringLiteral("input")
                                             : QStringLiteral("input%1").arg(slot_count);
                    slot_of_path.insert(path, slot);
                    if (bindings != nullptr) {
                        bindings->insert(slot, path);
                    }
                }
                item.dir.path = QStringLiteral("${%1}").arg(slot);
            }
        }
        preset.lists.append(item);
    }

    return preset;
}

}  // namespace batchsmith::core
