/// 预设（TOML）的测试。
///
/// 这一组要钉住的是**存下来的东西与读回来的东西完全一样**，尤其是几类容易被
/// 序列化悄悄改掉的内容：Windows 路径的反斜杠、模板里的 `$` 与引号、中文。
/// 预设文件是给人看、给人改的，所以可读性也要一起保住（用 TOML 字面字符串）。

#include <cstdio>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "batchsmith/core/list/dir_source.hpp"
#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/preset/preset.hpp"

#include "doctest/doctest.h"

namespace {

using batchsmith::core::bind_slots;
using batchsmith::core::bindings_path_for;
using batchsmith::core::ListPadding;
using batchsmith::core::ListSource;
using batchsmith::core::ListSourceKind;
using batchsmith::core::ListSourceList;
using batchsmith::core::load_bindings;
using batchsmith::core::load_preset;
using batchsmith::core::Preset;
using batchsmith::core::preset_from_sources;
using batchsmith::core::preset_from_toml;
using batchsmith::core::preset_slots;
using batchsmith::core::preset_sources;
using batchsmith::core::preset_to_toml;
using batchsmith::core::PresetList;
using batchsmith::core::save_bindings;
using batchsmith::core::save_preset;
using batchsmith::core::SlotBindings;
using batchsmith::core::slots_in;

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 一个典型预设：一个手输列表 + 一个绑定文件夹的列表。
[[nodiscard]] Preset sample_preset() {
    Preset preset;
    preset.name = S(u"番剧重命名");
    preset.level = S(u"safe");

    PresetList manual;
    manual.id = S(u"list1");
    manual.kind = ListSourceKind::Manual;
    manual.items = {S(u"01"), S(u"02"), S(u"09"), S(u"10")};
    manual.fill = ListPadding::Empty;
    preset.lists.append(manual);

    PresetList directory;
    directory.id = S(u"list2");
    directory.kind = ListSourceKind::Directory;
    directory.dir.path = S(u"${input}");
    directory.dir.filter = S(u"*.mkv");
    directory.dir.recursive = true;
    preset.lists.append(directory);

    preset.template_text = S(u"mv \"$list2[i]$\" \"第$list1[i]$话 正片.mkv\"");
    return preset;
}

/// 读写往返：应当逐字段一致。
void check_round_trip(const Preset& original) {
    const QString text = preset_to_toml(original);
    const auto loaded = preset_from_toml(text);
    if (!loaded.ok()) {
        // 往返失败时必须能同时看到「生成了什么」与「哪里解析不过去」——
        // 否则只看到一句 REQUIRE(false)，根本不知道从哪查
        // （这一条当场抓出过 `fill = empty` 漏加引号）。
        std::fprintf(stderr,
                     "=== 生成的 TOML ===\n%s\n=== 解析错误 ===\n%s\n",
                     text.toUtf8().constData(),
                     loaded.error.toUtf8().constData());
    }
    REQUIRE(loaded.ok());

    CHECK(loaded.preset.name == original.name);
    CHECK(loaded.preset.version == original.version);
    CHECK(loaded.preset.level == original.level);
    CHECK(loaded.preset.template_text == original.template_text);
    REQUIRE(loaded.preset.lists.size() == original.lists.size());

    for (qsizetype index = 0; index < original.lists.size(); ++index) {
        const auto& expected = original.lists.at(index);
        const auto& actual = loaded.preset.lists.at(index);
        CHECK(actual.id == expected.id);
        CHECK(actual.kind == expected.kind);
        CHECK(actual.fill == expected.fill);
        CHECK(actual.items == expected.items);
        CHECK(actual.dir.path == expected.dir.path);
        CHECK(actual.dir.filter == expected.dir.filter);
        CHECK(actual.dir.recursive == expected.dir.recursive);
        CHECK(actual.dir.include_dirs == expected.dir.include_dirs);
        CHECK(actual.dir.include_hidden == expected.dir.include_hidden);
    }
}

/// 造一个有几个文件的真实目录。
void make_files(const QString& directory, const QStringList& names) {
    for (const QString& name : names) {
        QFile file(QDir(directory).filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("x") == 1);
        file.close();
    }
}

}  // namespace

// ===========================================================================
// 序列化往返
// ===========================================================================

TEST_CASE("预设：往返一致（中文、引号、美元符、反斜杠路径）") {
    check_round_trip(sample_preset());
}

TEST_CASE("预设：Windows 反斜杠路径必须原样保住") {
    Preset preset;
    preset.name = S(u"路径转义");
    PresetList list;
    list.id = S(u"list1");
    list.kind = ListSourceKind::Directory;
    // 这一条是选 TOML **字面字符串**（单引号）的直接理由：用基本字符串时
    // `\a` 不是合法转义，整个文件会解析失败 —— 而 Windows 路径里全是反斜杠。
    list.dir.path = S(u"D:\\番剧\\2024");
    preset.lists.append(list);
    preset.template_text = S(u"$list1[i]$");

    const QString text = preset_to_toml(preset);
    CHECK_MESSAGE((text.contains(S(u"D:\\番剧\\2024"))), "文件里应当能直接看到原样的路径");

    check_round_trip(preset);
}

TEST_CASE("预设：模板里的单引号走基本字符串也不丢内容") {
    Preset preset;
    preset.name = S(u"带单引号");
    PresetList list;
    list.id = S(u"list1");
    list.items = {S(u"a'b")};
    preset.lists.append(list);
    // 模板里既有单引号又有双引号与反斜杠 —— 三条转义路径一起走
    preset.template_text = S(u"$replace(list1[i], \"'\", '’')$ \\d+ $list1[i]$");

    check_round_trip(preset);
}

TEST_CASE("预设：多行模板用转义后仍一致") {
    Preset preset;
    preset.name = S(u"多行");
    preset.template_text = S(u"第一行\n第二行\t带制表符");

    check_round_trip(preset);
}

TEST_CASE("预设：空预设（没有列表、没有模板）也能往返") {
    Preset preset;
    preset.name = S(u"空");

    check_round_trip(preset);
}

TEST_CASE("预设：写出的文件带说明注释，人打开能看懂") {
    const QString text = preset_to_toml(sample_preset());

    CHECK(text.contains(S(u"[preset]")));
    CHECK(text.contains(S(u"[[lists]]")));
    CHECK(text.contains(S(u"[output]")));
    CHECK(text.contains(S(u"${input}")));
    CHECK_MESSAGE((text.contains(S(u"local.toml"))), "要告诉读者本机绑定存在哪、别一起分享");
}

// ===========================================================================
// 槽位
// ===========================================================================

TEST_CASE("槽位：提取出现的名字（保序、去重、忽略非法字符）") {
    CHECK(slots_in(S(u"${input}")) == QStringList{S(u"input")});
    CHECK(slots_in(S(u"${input}/番剧")) == QStringList{S(u"input")});
    CHECK(slots_in(S(u"${a}/${b}/${a}")) == QStringList{S(u"a"), S(u"b")});
    CHECK(slots_in(S(u"没有槽位")) == QStringList{});
    CHECK_MESSAGE((slots_in(S(u"${}")) == QStringList{}), "空名字不算槽位");
    CHECK_MESSAGE((slots_in(S(u"${a b}")) == QStringList{}), "名字里有空格不算槽位");
    CHECK_MESSAGE((slots_in(S(u"${未闭合")) == QStringList{}), "没闭合就当没有");
    CHECK(slots_in(S(u"${in_put2}")) == QStringList{S(u"in_put2")});
}

TEST_CASE("槽位：替换；未绑定的原样保留") {
    SlotBindings bindings;
    bindings.insert(S(u"input"), S(u"D:/anime"));
    bindings.insert(S(u"out"), S(u"D:/done"));

    CHECK(bind_slots(S(u"${input}"), bindings) == S(u"D:/anime"));
    CHECK(bind_slots(S(u"${input}/番剧"), bindings) == S(u"D:/anime/番剧"));
    CHECK(bind_slots(S(u"${input}|${out}"), bindings) == S(u"D:/anime|D:/done"));
    CHECK(bind_slots(S(u"没有槽位"), bindings) == S(u"没有槽位"));

    // 未绑定时保留 `${...}`：于是它会在扫描时报「文件夹不存在：${other}」，
    // 一眼看得出是"还没绑定"，而不是被换成空路径后报一个莫名其妙的错
    CHECK(bind_slots(S(u"${other}/x"), bindings) == S(u"${other}/x"));
    CHECK(bind_slots(S(u"${input}/${other}"), bindings) == S(u"D:/anime/${other}"));
}

TEST_CASE("槽位：从预设里汇总（按列表顺序）") {
    Preset preset;
    PresetList first;
    first.id = S(u"list1");
    first.kind = ListSourceKind::Directory;
    first.dir.path = S(u"${input}");
    preset.lists.append(first);

    PresetList second;
    second.id = S(u"list2");
    second.kind = ListSourceKind::Directory;
    second.dir.path = S(u"${input}/${extra}");
    preset.lists.append(second);

    CHECK(preset_slots(preset) == QStringList{S(u"input"), S(u"extra")});
}

// ===========================================================================
// 保存方向：界面状态 → 预设（含槽位化）
// ===========================================================================

TEST_CASE("保存：文件夹路径被槽位化，同一路径共用一个槽位") {
    ListSourceList sources;
    sources.append(ListSource{S(u"list1"), {S(u"01"), S(u"02")}});

    ListSource by_filter_a;
    by_filter_a.name = S(u"list2");
    by_filter_a.kind = ListSourceKind::Directory;
    by_filter_a.dir.path = S(u"D:/anime");
    by_filter_a.dir.filter = S(u"*.mkv");
    sources.append(by_filter_a);

    // 同一个文件夹、另一个过滤条件 —— 文件里不该出现两次路径
    ListSource by_filter_b;
    by_filter_b.name = S(u"list3");
    by_filter_b.kind = ListSourceKind::Directory;
    by_filter_b.dir.path = S(u"D:/anime");
    by_filter_b.dir.filter = S(u"*.mp4");
    sources.append(by_filter_b);

    // 另一个文件夹 → 第二个槽位
    ListSource other;
    other.name = S(u"list4");
    other.kind = ListSourceKind::Directory;
    other.dir.path = S(u"D:/other");
    sources.append(other);

    SlotBindings bindings;
    const Preset preset = preset_from_sources(sources, S(u"$list1[i]$"), S(u"我的预设"), &bindings);

    REQUIRE(preset.lists.size() == 4);
    CHECK(preset.lists.at(0).kind == ListSourceKind::Manual);
    CHECK(preset.lists.at(0).items == QStringList{S(u"01"), S(u"02")});
    CHECK(preset.lists.at(1).dir.path == S(u"${input}"));
    CHECK_MESSAGE((preset.lists.at(2).dir.path == S(u"${input}")), "同一路径共用一个槽位");
    CHECK(preset.lists.at(3).dir.path == S(u"${input2}"));

    CHECK(bindings.value(S(u"input")) == S(u"D:/anime"));
    CHECK(bindings.value(S(u"input2")) == S(u"D:/other"));
    CHECK(bindings.size() == 2);

    // 预设里**不含**绝对路径 —— 这是它能进版本控制、能分享的前提
    const QString text = preset_to_toml(preset);
    CHECK_FALSE(text.contains(S(u"D:/anime")));
    CHECK_FALSE(text.contains(S(u"D:/other")));
}

TEST_CASE("保存：还没绑定路径的文件夹列不产生槽位") {
    ListSource pending;
    pending.name = S(u"list1");
    pending.kind = ListSourceKind::Directory;
    pending.dir.path = QString();  // 切到文件夹模式但还没选目录

    SlotBindings bindings;
    const Preset preset = preset_from_sources({pending}, S(u"x"), S(u"n"), &bindings);

    REQUIRE(preset.lists.size() == 1);
    CHECK(preset.lists.at(0).dir.path.isEmpty());
    CHECK(bindings.isEmpty());
}

// ===========================================================================
// 加载方向：预设 → 界面状态
// ===========================================================================

TEST_CASE("加载：槽位替换后真的去读了文件夹") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    make_files(dir.path(), {S(u"第1话.mkv"), S(u"第2话.mkv"), S(u"第10话.mkv"), S(u"说明.txt")});

    Preset preset;
    preset.name = S(u"读目录");
    PresetList list;
    list.id = S(u"list1");
    list.kind = ListSourceKind::Directory;
    list.dir.path = S(u"${input}");
    list.dir.filter = S(u"*.mkv");
    preset.lists.append(list);

    SlotBindings bindings;
    bindings.insert(S(u"input"), dir.path());

    QHash<QString, QString> errors;
    const ListSourceList sources = preset_sources(preset, bindings, &errors);

    REQUIRE(sources.size() == 1);
    CHECK(errors.isEmpty());
    CHECK(sources.at(0).items == QStringList{S(u"第1话.mkv"), S(u"第2话.mkv"), S(u"第10话.mkv")});
}

TEST_CASE("加载：单列读不到时不影响其它列，原因按列表 id 带出") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    Preset preset;
    PresetList manual;
    manual.id = S(u"list1");
    manual.items = {S(u"a"), S(u"b")};
    preset.lists.append(manual);

    PresetList broken;
    broken.id = S(u"list2");
    broken.kind = ListSourceKind::Directory;
    broken.dir.path = S(u"${input}");  // 绑定到一个不存在的地方
    preset.lists.append(broken);

    SlotBindings bindings;
    bindings.insert(S(u"input"), QDir(dir.path()).filePath(S(u"没有这个目录")));

    QHash<QString, QString> errors;
    const ListSourceList sources = preset_sources(preset, bindings, &errors);

    REQUIRE(sources.size() == 2);
    CHECK_MESSAGE((sources.at(0).items == QStringList{S(u"a"), S(u"b")}), "手输列不受影响");
    CHECK(sources.at(1).items.isEmpty());
    REQUIRE(errors.size() == 1);
    CHECK(errors.contains(S(u"list2")));
    CHECK(errors.value(S(u"list2")).contains(S(u"不存在")));
}

TEST_CASE("加载：未绑定的槽位会以可读的方式暴露出来") {
    Preset preset;
    PresetList list;
    list.id = S(u"list1");
    list.kind = ListSourceKind::Directory;
    list.dir.path = S(u"${input}");
    preset.lists.append(list);

    QHash<QString, QString> errors;
    const ListSourceList sources = preset_sources(preset, {}, &errors);

    REQUIRE(sources.size() == 1);
    REQUIRE(errors.size() == 1);
    // 报错里带着 `${input}`，用户一看就知道是没绑定，而不是"路径写错了"
    CHECK(errors.value(S(u"list1")).contains(S(u"${input}")));
}

// ===========================================================================
// 错误路径
// ===========================================================================

TEST_CASE("解析：坏 TOML 要给出位置") {
    const auto loaded = preset_from_toml(S(u"[preset\nname = 'x'"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"TOML")));
    CHECK(loaded.error.contains(S(u"行")));
}

TEST_CASE("解析：缺少 [preset] 段要报错") {
    const auto loaded = preset_from_toml(S(u"[[lists]]\nid = 'list1'\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"[preset]")));
}

TEST_CASE("解析：列表缺 id 要报错（id 是表达式引用它的名字）") {
    const auto loaded =
            preset_from_toml(S(u"[preset]\nname = 'x'\n\n[[lists]]\nkind = 'manual'\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"id")));
}

TEST_CASE("解析：还不支持的来源要明确报错，而不是静默忽略") {
    // 示例预设里曾写过 kind = 'snapshot'（规划里有、当时还没实现）。
    // 这种「写了但没实现」的字段最危险：静默忽略会让人以为它生效了。
    const auto loaded = preset_from_toml(S(u"[preset]\nname='x'\n\n[[lists]]\nid='list2'\n"
                                           "source = { kind = 'snapshot', path = 'a.txt' }\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"snapshot")));
    CHECK(loaded.error.contains(S(u"还不支持")));
}

TEST_CASE("解析：sort 只认 natural（排序固定自然序）") {
    const auto loaded =
            preset_from_toml(S(u"[preset]\nname='x'\n\n[[lists]]\nid='list1'\n"
                               "source = { kind = 'dir', path = '/a', sort = 'mtime' }\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"mtime")));
    CHECK(loaded.error.contains(S(u"自然序")));

    const auto explicit_natural =
            preset_from_toml(S(u"[preset]\nname='x'\n\n[[lists]]\nid='list1'\n"
                               "source = { kind = 'dir', path = '/a', sort = 'natural' }\n"));
    CHECK(explicit_natural.ok());
}

TEST_CASE("解析：source 不是表时报错并给出写法") {
    const auto loaded =
            preset_from_toml(S(u"[preset]\nname='x'\n\n[[lists]]\nid='list1'\nsource = 'dir'\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"source")));
    CHECK(loaded.error.contains(S(u"kind")));
}

TEST_CASE("解析：output.mode 只认 rename（还不支持的写法当场报错）") {
    const auto loaded = preset_from_toml(S(u"[preset]\nname='x'\n\n[output]\nmode='argv'\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"argv")));
    CHECK(loaded.error.contains(S(u"rename")));

    const auto rename = preset_from_toml(S(u"[preset]\nname='x'\n\n[output]\nmode='rename'\n"));
    CHECK(rename.ok());
    CHECK(rename.preset.output_mode == S(u"rename"));

    const auto missing = preset_from_toml(S(u"[preset]\nname='x'\n"));
    CHECK(missing.ok());
    // 缺省就是 rename
    CHECK(missing.preset.output_mode == S(u"rename"));
}

TEST_CASE("解析：类型不对要报错，而不是静默忽略") {
    // 布尔字段写成字符串 —— 静默忽略会让"预设看起来加载成功了，实际少了半截设置"
    const auto loaded =
            preset_from_toml(S(u"[preset]\nname='x'\n\n[[lists]]\nid='list1'\n"
                               "source = { kind = 'dir', path = '/a', recursive = 'yes' }\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"recursive")));
    CHECK(loaded.error.contains(S(u"true / false")));
}

TEST_CASE("解析：更高版本的预设直接拒绝，不猜着读") {
    const auto loaded = preset_from_toml(S(u"[preset]\nname='x'\nversion=99\n"));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"99")));
    CHECK(loaded.error.contains(S(u"升级")));
}

TEST_CASE("解析：版本的默认值与允许值") {
    const auto current = preset_from_toml(S(u"[preset]\nname='x'\nversion=1\n"));
    CHECK(current.ok());
    CHECK(current.preset.version == 1);

    const auto older = preset_from_toml(S(u"[preset]\nname='x'\nversion=0\n"));
    CHECK_MESSAGE((older.ok()), "低版本文件仍然可读");

    const auto missing = preset_from_toml(S(u"[preset]\nname='x'\n"));
    CHECK(missing.ok());
    CHECK(missing.preset.version == batchsmith::core::kPresetVersion);
}

TEST_CASE("解析：未知字段被忽略（旧版本仍能打开新版本加过字段的预设）") {
    const auto loaded = preset_from_toml(S(u"[preset]\nname='x'\nfuture_field = 42\n\n"
                                           "[output]\ntemplate = 'x'\nfuture = 'y'\n"));
    CHECK(loaded.ok());
    CHECK(loaded.preset.template_text == S(u"x"));
}

TEST_CASE("解析：手写的精简预设（只有 id 与 items）也能用") {
    const auto loaded = preset_from_toml(S(u"[preset]\nname='极简'\n\n"
                                           "[[lists]]\nid='list1'\nitems=['a','b']\n"));
    REQUIRE(loaded.ok());
    REQUIRE(loaded.preset.lists.size() == 1);
    // kind 缺省就是 manual
    CHECK(loaded.preset.lists.at(0).kind == ListSourceKind::Manual);
    CHECK(loaded.preset.lists.at(0).items == QStringList{S(u"a"), S(u"b")});
}

// ===========================================================================
// 文件
// ===========================================================================

TEST_CASE("文件：预设保存后能原样读回，并记住来源路径") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(S(u"我的预设.toml"));

    QString error;
    CHECK(save_preset(sample_preset(), path, &error));
    CHECK(error.isEmpty());
    CHECK(QFileInfo::exists(path));

    const auto loaded = load_preset(path);
    REQUIRE(loaded.ok());
    CHECK(loaded.preset.name == S(u"番剧重命名"));
    CHECK(loaded.preset.file_path == path);
    CHECK(loaded.preset.template_text == sample_preset().template_text);
}

TEST_CASE("文件：读不到的路径给出明确错误") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto loaded = load_preset(QDir(dir.path()).filePath(S(u"没有这个文件.toml")));
    CHECK_FALSE(loaded.ok());
    CHECK(loaded.error.contains(S(u"打不开")));
}

TEST_CASE("文件：没有 name 的预设用文件名兜底") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(S(u"番剧方案.toml"));

    QString error;
    CHECK(save_preset(sample_preset(), path, &error));

    // 手写的预设常常不写 name —— 标题栏与「最近打开」总得有个能认的名字
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    QString text = QString::fromUtf8(file.readAll());
    text.replace(S(u"name = '番剧重命名'"), S(u"name = ''"));
    const QByteArray bytes = text.toUtf8();
    REQUIRE(file.resize(0));
    REQUIRE(file.write(bytes) == bytes.size());
    file.close();

    const auto loaded = load_preset(path);
    REQUIRE(loaded.ok());
    CHECK(loaded.preset.name == S(u"番剧方案"));
}

TEST_CASE("文件：伴生绑定文件的路径规则") {
    // 用真实临时目录而不是 /tmp：Windows 上 `/tmp` 会被当成相对路径并并到当前盘，
    // 那样断言比的是两段不同的绝对路径。
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir base(dir.path());

    CHECK(bindings_path_for(base.filePath(S(u"番剧.toml"))) ==
          base.filePath(S(u"番剧.local.toml")));
    CHECK(bindings_path_for(base.filePath(S(u"a.b.toml"))) == base.filePath(S(u"a.b.local.toml")));
    CHECK(bindings_path_for(base.filePath(S(u"noext"))) == base.filePath(S(u"noext.local")));
}

TEST_CASE("文件：绑定往返；没绑过时不算错") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString preset_path = QDir(dir.path()).filePath(S(u"p.toml"));

    QString error;
    CHECK(save_preset(sample_preset(), preset_path, &error));

    // 没绑过 → 空表且不报错
    QString bind_error;
    CHECK(load_bindings(preset_path, &bind_error).isEmpty());
    CHECK(bind_error.isEmpty());

    SlotBindings bindings;
    bindings.insert(S(u"input"), S(u"D:/anime"));
    bindings.insert(S(u"extra"), S(u"E:/其它"));
    CHECK(save_bindings(bindings, preset_path, &error));
    CHECK(error.isEmpty());
    CHECK(QFileInfo::exists(bindings_path_for(preset_path)));

    QString read_error;
    const SlotBindings read = load_bindings(preset_path, &read_error);
    CHECK(read_error.isEmpty());
    CHECK(read == bindings);
}

TEST_CASE("文件：绑定文件坏掉时要报错（否则「绑定莫名丢了」没法查）") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString preset_path = QDir(dir.path()).filePath(S(u"p.toml"));

    QFile file(bindings_path_for(preset_path));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[bindings\ninput = 'x'\n");
    file.close();

    QString error;
    CHECK(load_bindings(preset_path, &error).isEmpty());
    CHECK_FALSE(error.isEmpty());
    CHECK(error.contains(S(u"TOML")));
}

// ===========================================================================
// 仓库里那份示例预设
// ===========================================================================

TEST_CASE("示例预设必须始终可解析（它会随时间腐坏，得有人盯着）") {
    const QString path = QStringLiteral(BATCHSMITH_SOURCE_DIR "/presets/example-rename.toml");
    const auto loaded = load_preset(path);
    REQUIRE_MESSAGE(loaded.ok(), loaded.error.toStdString());

    const Preset& preset = loaded.preset;
    CHECK(preset.name == S(u"番剧重命名"));
    CHECK(preset.version == batchsmith::core::kPresetVersion);
    REQUIRE(preset.lists.size() == 2);
    CHECK_FALSE(preset.template_text.isEmpty());

    // list1 绑定文件夹，且**不存绝对路径**（这是它可分享的前提）
    CHECK(preset.lists.at(0).id == S(u"list1"));
    CHECK(preset.lists.at(0).kind == ListSourceKind::Directory);
    CHECK(preset.lists.at(0).dir.path.startsWith(S(u"${")));
    CHECK(preset.lists.at(0).dir.filter == S(u"*.mkv"));

    // list2 手输
    CHECK(preset.lists.at(1).id == S(u"list2"));
    CHECK(preset.lists.at(1).kind == ListSourceKind::Manual);
    CHECK_FALSE(preset.lists.at(1).items.isEmpty());

    // 示例里用到的槽位就是 input
    CHECK(preset_slots(preset) == QStringList{S(u"input")});

    // 存下来的东西能执行：用示例模板求一次值（绑一个临时目录）
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    make_files(dir.path(), {S(u"第1话.mkv"), S(u"第2话.mkv")});
    SlotBindings bindings;
    bindings.insert(S(u"input"), dir.path());

    QHash<QString, QString> errors;
    const ListSourceList sources = preset_sources(preset, bindings, &errors);
    CHECK(errors.isEmpty());
    REQUIRE(sources.size() == 2);
    CHECK(sources.at(0).items.size() == 2);
}
