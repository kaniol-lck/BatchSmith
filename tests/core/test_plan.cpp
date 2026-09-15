/// 计划（Plan）的测试。
///
/// `bs plan` 是"动文件"之前的最后一道闸门，所以这一组要钉住的是三件事：
///
/// 1. **行与文件的对应关系**：第几行是哪个文件、按什么顺序 —— 用户核对的正是这个
///    （技术方案 §4.4：绝不按序号回查目录）；
/// 2. **问题行当场被认出来**：源不在、两条输出同名、目标已存在、目标跑出绑定根……
///    这些都不该拖到执行时才炸；
/// 3. **两条 dry-run 的硬要求**：`build_plan()` **不产生任何副作用**，以及同一个预设
///    两次算出的结果**逐字相同**（`rand` 有固定种子）—— 这正是 P3 验收标准的第 2、3 条。

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "batchsmith/core/list/list_source.hpp"
#include "batchsmith/core/plan/plan.hpp"
#include "batchsmith/core/preset/preset.hpp"

#include "doctest/doctest.h"

namespace {

using batchsmith::core::ListPadding;
using batchsmith::core::ListSourceKind;
using batchsmith::core::Preset;
using batchsmith::core::PresetList;
using batchsmith::core::SlotBindings;
using batchsmith::core::plan::build_plan;
using batchsmith::core::plan::Plan;
using batchsmith::core::plan::plan_to_json;
using batchsmith::core::plan::plan_to_text;
using batchsmith::core::plan::PlanRow;
using batchsmith::core::plan::RowState;

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 一个真实的小目录。用真文件而不是桩：自然序、glob、`QFileInfo::exists()`
/// 全都是文件系统行为，桩不出来。
///
/// 顶层内容：`第1话.mkv` `第2话.mkv` `第10话.mkv` `file2.mkv` `file10.mkv` `readme.txt`
/// （`*.mkv` 共 5 个；`readme.txt` 应当被过滤掉）
class Fixture {
public:
    Fixture() {
        REQUIRE(m_dir.isValid());
        touch(S(u"第1话.mkv"));
        touch(S(u"第2话.mkv"));
        touch(S(u"第10话.mkv"));
        touch(S(u"file2.mkv"));
        touch(S(u"file10.mkv"));
        touch(S(u"readme.txt"));
    }

    /// 临时目录路径（测试结束后自动删掉）。
    [[nodiscard]] QString path() const { return m_dir.path(); }

    [[nodiscard]] SlotBindings bindings() const {
        SlotBindings bindings;
        bindings.insert(S(u"input"), m_dir.path());
        return bindings;
    }

    /// 往目录里放一个空文件（用来造"目标已存在"这类场景）。
    void touch(const QString& relative) const {
        const QString full = QDir(m_dir.path()).filePath(relative);
        REQUIRE(QDir().mkpath(QFileInfo(full).absolutePath()));
        QFile file(full);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write("x", 1) == 1);
    }

private:
    QTemporaryDir m_dir;
};

/// 一个最小的 rename 预设：一列绑定文件夹（槽位 `${input}`），模板逐行产出新名字。
[[nodiscard]] Preset rename_preset(const QString& template_text,
                                   const QString& filter = QStringLiteral("*.mkv")) {
    Preset preset;
    preset.name = S(u"测试预设");
    preset.output_mode = QStringLiteral("rename");
    preset.template_text = template_text;

    PresetList files;
    files.id = S(u"list1");
    files.kind = ListSourceKind::Directory;
    files.dir.path = S(u"${input}");
    files.dir.filter = filter;
    preset.lists.append(files);
    return preset;
}

/// 再挂一列手输列表（`list2`…），用来构造逐行不同的目标名。
void add_manual_list(Preset* preset,
                     const QString& id,
                     const QStringList& items,
                     ListPadding fill = ListPadding::Empty) {
    PresetList manual;
    manual.id = id;
    manual.kind = ListSourceKind::Manual;
    manual.items = items;
    manual.fill = fill;
    preset->lists.append(manual);
}

/// 在计划里找某个源在第几行（找不到给 -1）。行号是 1 起。
[[nodiscard]] int row_of_source(const Plan& plan, const QString& source) {
    for (const PlanRow& row : plan.rows) {
        if (row.source == source) {
            return row.index;
        }
    }
    return -1;
}

/// 目录快照：条目名 + 大小 + 修改时间。用来证明"计划这一步什么都没写"。
[[nodiscard]] QStringList snapshot(const QString& directory) {
    QStringList entries;
    const QFileInfoList infos = QDir(directory).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo& info : infos) {
        entries.append(QStringLiteral("%1|%2|%3")
                               .arg(info.fileName())
                               .arg(info.size())
                               .arg(info.lastModified().toMSecsSinceEpoch()));
    }
    return entries;
}

}  // namespace

// ---------------------------------------------------------------------------
// 行 ↔ 文件
// ---------------------------------------------------------------------------

TEST_CASE("计划：每行绑到真实文件，条目按自然序（file2 在 file10 之前）") {
    const Fixture fx;
    const Preset preset = rename_preset(S(u"$stem(list1[i])$-新.mkv"));
    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());

    const Plan& plan = built.plan;
    CHECK(plan.file_list == S(u"list1"));
    CHECK(!plan.root.isEmpty());
    CHECK(plan.rows.size() == 5);  // readme.txt 被 glob 挡掉
    CHECK_FALSE(plan.has_problems());
    CHECK(plan.problem_count() == 0);
    CHECK(plan.count(RowState::Ready) == 5);

    // 自然序：这是 P3 验收标准的第 1 条
    CHECK(row_of_source(plan, S(u"file2.mkv")) < row_of_source(plan, S(u"file10.mkv")));
    CHECK(row_of_source(plan, S(u"第2话.mkv")) < row_of_source(plan, S(u"第10话.mkv")));

    // 行号从 1 起、连续，且每行都指向一个真实存在的文件
    int expected_index = 1;
    for (const PlanRow& row : plan.rows) {
        CHECK(row.index == expected_index);
        ++expected_index;
        CHECK(QFileInfo::exists(row.source_path));
        CHECK(row.source_path.endsWith(row.source));
        CHECK(row.target == QFileInfo(row.source).completeBaseName() + S(u"-新.mkv"));
        CHECK(row.state == RowState::Ready);
    }
}

TEST_CASE("计划：渲染出标题、统计与每一行的源与目标") {
    const Fixture fx;
    const Preset preset = rename_preset(S(u"$stem(list1[i])$-新.mkv"));
    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());

    const QString text = plan_to_text(built.plan);
    CHECK(text.contains(S(u"计划 · 测试预设")));
    CHECK(text.contains(S(u"共 5 行：可执行 5 · 无需动作 0 · 有问题 0")));
    CHECK(text.contains(S(u"文件维度    list1")));
    CHECK(text.contains(S(u"第1话.mkv")));
    CHECK(text.contains(S(u"→")));
    // 目标名也要真的在输出里 —— 只报"第几行"等于没说
    CHECK(text.contains(S(u"第1话-新.mkv")));
}

// ---------------------------------------------------------------------------
// 问题行
// ---------------------------------------------------------------------------

TEST_CASE("计划：目标已存在（而且不是这批里的文件）被认出来") {
    Fixture fx;
    // `.bak` 不在 `*.mkv` 的列表里 —— 它是个"外人"，被撞上就是真冲突
    fx.touch(S(u"占位.bak"));

    Preset preset = rename_preset(S(u"$list2[i]$"));
    add_manual_list(&preset,
                    S(u"list2"),
                    {S(u"占位.bak"), S(u"新2.mkv"), S(u"新3.mkv"), S(u"新4.mkv"), S(u"新5.mkv")});

    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());
    const Plan& plan = built.plan;
    CHECK(plan.rows.size() == 5);
    CHECK(plan.count(RowState::TargetExists) == 1);
    CHECK(plan.count(RowState::Ready) == 4);
    CHECK(plan.has_problems());

    // 报出来的必须是撞上的那一行
    bool found = false;
    for (const PlanRow& row : plan.rows) {
        if (row.state == RowState::TargetExists) {
            found = true;
            CHECK(row.index == 1);
            CHECK(row.target == S(u"占位.bak"));
            CHECK(row.note.contains(S(u"已存在")));
        }
    }
    CHECK(found);
}

TEST_CASE("计划：两条输出同名 → 每一行都被标出来，并指出与哪一行撞了") {
    const Fixture fx;
    Preset preset = rename_preset(S(u"$list2[i]$"));
    add_manual_list(
            &preset,
            S(u"list2"),
            {S(u"same.mkv"), S(u"same.mkv"), S(u"same.mkv"), S(u"same.mkv"), S(u"same.mkv")});

    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());
    const Plan& plan = built.plan;
    CHECK(plan.count(RowState::DuplicateTarget) == 5);
    CHECK(plan.count(RowState::Ready) == 0);
    // 第一条也要说清"跟谁撞了"，而不是指到自己头上（"与第 1 行同名"出现在第 1 行）
    CHECK(!plan.rows.first().note.contains(S(u"与第 1 行同名")));
    CHECK(plan.rows.at(1).note.contains(S(u"与第 1 行同名")));
}

TEST_CASE("计划：同一个文件被两行用到（缺省方式 repeat 时最典型）") {
    const Fixture fx;
    // 文件维度只有 1 个条目（glob 只留一个），手输列表有 3 项 ⇒ 另外两行会回绕到同一个文件
    Preset preset = rename_preset(S(u"$list2[i]$"), S(u"第1话.mkv"));
    add_manual_list(&preset, S(u"list2"), {S(u"甲.mkv"), S(u"乙.mkv"), S(u"丙.mkv")});
    preset.lists.first().fill = ListPadding::Repeat;

    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());
    const Plan& plan = built.plan;
    CHECK(plan.rows.size() == 3);
    CHECK(plan.count(RowState::DuplicateSource) == 3);
}

TEST_CASE("计划：目标跑出绑定文件夹的一律拒绝") {
    const Fixture fx;

    const auto check_rejected = [&fx](const QString& template_text, const QString& expect_note) {
        const auto built = build_plan(rename_preset(template_text), fx.bindings());
        REQUIRE(built.ok());
        CHECK(built.plan.count(RowState::TargetNotInside) == 5);
        CHECK(built.plan.has_problems());
        CHECK(built.plan.rows.first().note.contains(expect_note));
    };

    check_rejected(S(u"../$list1[i]$"), S(u"之外"));      // 往上一层
    check_rejected(S(u"/$list1[i]$"), S(u"绝对路径"));    // 绝对路径
    check_rejected(S(u"$list1[i]$.mkv/"), S(u"分隔符"));  // 那是个目录
}

TEST_CASE("计划：求值结果是空串时明确报出来") {
    const Fixture fx;
    Preset preset = rename_preset(S(u"$list2[i]$"));
    add_manual_list(&preset, S(u"list2"), {QString(), QString(), QString(), QString(), QString()});

    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());
    CHECK(built.plan.count(RowState::TargetEmpty) == 5);
    CHECK(built.plan.rows.first().note.contains(S(u"空串")));
}

TEST_CASE("计划：目标与源同名 → 无需动作，不算问题") {
    const Fixture fx;
    const auto built = build_plan(rename_preset(S(u"$list1[i]$")), fx.bindings());
    REQUIRE(built.ok());
    CHECK(built.plan.count(RowState::Unchanged) == 5);
    CHECK_FALSE(built.plan.has_problems());  // "本来就对"不该让 dry-run 报失败
}

TEST_CASE("计划：没有文件夹来源 → 每行都说清没有对应的文件") {
    Preset preset;
    preset.name = S(u"纯手输");
    preset.template_text = S(u"$list1[i]$.txt");
    add_manual_list(&preset, S(u"list1"), {S(u"甲"), S(u"乙")});

    const auto built = build_plan(preset, {});
    REQUIRE(built.ok());  // 算得出来：名字仍然值得看一眼
    CHECK(built.plan.root.isEmpty());
    CHECK(built.plan.file_list.isEmpty());
    CHECK(built.plan.count(RowState::NoSource) == 2);
    CHECK(built.plan.has_problems());
    CHECK(!built.plan.notes.isEmpty());
}

// ---------------------------------------------------------------------------
// 算不出来时要说清原因
// ---------------------------------------------------------------------------

TEST_CASE("计划：模板没有逐行产出名字 → 报错，而不是硬凑一份计划") {
    const Fixture fx;
    // 没用 i / rows ⇒ 整批只求值一次，得到 1 行；而输入有 5 行
    const auto built = build_plan(rename_preset(S(u"fixed.mkv")), fx.bindings());
    CHECK_FALSE(built.ok());
    CHECK(built.error.contains(S(u"一行一个名字")));
}

TEST_CASE("计划：两个文件夹来源 → 报错并列出候选，不替用户挑一个") {
    const Fixture fx;
    Preset preset = rename_preset(S(u"$list1[i]$"));
    PresetList second;
    second.id = S(u"list2");
    second.kind = ListSourceKind::Directory;
    second.dir.path = S(u"${input}");
    preset.lists.append(second);

    const auto built = build_plan(preset, fx.bindings());
    CHECK_FALSE(built.ok());
    CHECK(built.error.contains(S(u"list1")));
    CHECK(built.error.contains(S(u"list2")));
}

TEST_CASE("计划：路径绑错 → 报错并指出是哪一列，不给一份空计划") {
    const Fixture fx;
    const auto built = build_plan(rename_preset(S(u"$list1[i]$")), {});  // 故意不绑
    CHECK_FALSE(built.ok());
    CHECK(built.error.contains(S(u"list1")));
}

// ---------------------------------------------------------------------------
// dry-run 的两条硬要求
// ---------------------------------------------------------------------------

TEST_CASE("计划：零副作用 —— 算一次计划，目录里的东西一个字节都没变") {
    const Fixture fx;
    const QStringList before = snapshot(fx.path());

    const auto built = build_plan(rename_preset(S(u"$stem(list1[i])$-改名.mkv")), fx.bindings());
    REQUIRE(built.ok());
    CHECK(built.plan.rows.size() == 5);

    CHECK(snapshot(fx.path()) == before);
}

TEST_CASE("计划：可复现 —— 同一个预设两次算出的结果逐字相同（rand 有固定种子）") {
    const Fixture fx;
    Preset preset = rename_preset(S(u"$list2[i]$-v$rand(99999)$.mkv"));
    add_manual_list(&preset, S(u"list2"), {S(u"甲"), S(u"乙"), S(u"丙"), S(u"丁"), S(u"戊")});

    const auto first = build_plan(preset, fx.bindings());
    const auto second = build_plan(preset, fx.bindings());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(plan_to_text(first.plan) == plan_to_text(second.plan));
    CHECK(plan_to_json(first.plan) == plan_to_json(second.plan));

    // 随机名确实随了行（否则这条测试什么也没证明）
    CHECK(first.plan.rows.first().target != first.plan.rows.at(1).target);
}

// ---------------------------------------------------------------------------
// 序列化
// ---------------------------------------------------------------------------

TEST_CASE("计划：JSON 与文本说的是同一件事") {
    Fixture fx;
    fx.touch(S(u"撞名.bak"));  // 同样是不在 `*.mkv` 列表里的"外人"

    Preset preset = rename_preset(S(u"$list2[i]$"));
    add_manual_list(&preset,
                    S(u"list2"),
                    {S(u"撞名.bak"), S(u"新2.mkv"), S(u"新3.mkv"), S(u"新4.mkv"), S(u"新5.mkv")});

    const auto built = build_plan(preset, fx.bindings());
    REQUIRE(built.ok());

    QJsonParseError parse_error{};
    const QJsonDocument doc =
            QJsonDocument::fromJson(plan_to_json(built.plan).toUtf8(), &parse_error);
    REQUIRE(parse_error.error == QJsonParseError::NoError);

    const QJsonObject root = doc.object();
    CHECK(root.value(QStringLiteral("output")).toString() == QStringLiteral("rename"));
    CHECK(root.value(QStringLiteral("file_list")).toString() == S(u"list1"));
    CHECK(!root.value(QStringLiteral("root")).toString().isEmpty());

    const QJsonObject counts = root.value(QStringLiteral("counts")).toObject();
    CHECK(counts.value(QStringLiteral("rows")).toInt() == 5);
    CHECK(counts.value(QStringLiteral("problems")).toInt() == 1);

    const QJsonArray rows = root.value(QStringLiteral("rows")).toArray();
    REQUIRE(rows.size() == 5);
    // 状态名是给脚本用的稳定 ASCII 词，别跟着中文标签改
    CHECK(rows.first().toObject().value(QStringLiteral("state")).toString() ==
          QStringLiteral("target_exists"));
    CHECK(rows.at(1).toObject().value(QStringLiteral("state")).toString() ==
          QStringLiteral("ready"));

    // 绝对路径也要给出来：Phase 4 的执行器要拿它去动文件
    const QString first_source = rows.first().toObject().value(QStringLiteral("source")).toString();
    CHECK(!first_source.isEmpty());
    CHECK(rows.first()
                  .toObject()
                  .value(QStringLiteral("source_path"))
                  .toString()
                  .endsWith(first_source));
}
