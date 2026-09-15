#include "batchsmith/core/dsl/cheatsheet.hpp"

#include "batchsmith/core/version.hpp"

namespace batchsmith::core::dsl {
namespace {

const QString kGroupSyntax = QStringLiteral("基本形态");
const QString kGroupRows = QStringLiteral("行与列表");
const QString kGroupExpand = QStringLiteral("组合与展开");
const QString kGroupHelper = QStringLiteral("工具函数");
const QString kGroupLua = QStringLiteral("Lua 原生");
const QString kGroupEdge = QStringLiteral("转义与边界");

/// 顶层章节：**纯文本版与 HTML 版共用这一份定义**，标题只写一次。
///
/// 加章节时改这里，两版输出同时生效 —— 否则迟早出现"HTML 里有、文本版没有"
/// 或者两版标题措辞不一致（那正是这个模块想避免的漂移）。
const QList<CheatSection>& sections() {
    static const QList<CheatSection> kList = {
            {QStringLiteral("sec-overview"), QStringLiteral("一、模板长什么样")},
            {QStringLiteral("sec-names"), QStringLiteral("二、区段里能拿到的名字")},
            {QStringLiteral("sec-lists"), QStringLiteral("三、列表从哪来（手输 / 绑定文件夹）")},
            {QStringLiteral("sec-rows"), QStringLiteral("四、行数与展开")},
            {QStringLiteral("sec-helpers"), QStringLiteral("五、工具函数")},
            {QStringLiteral("sec-lua"), QStringLiteral("六、沙箱里可用的 Lua")},
            {QStringLiteral("sec-pitfalls"), QStringLiteral("七、容易踩的几个点")},
            {QStringLiteral("sec-examples"),
             QStringLiteral("八、示例（下面每一条都会被自动测试跑一遍）")},
            {QStringLiteral("sec-preset"), QStringLiteral("九、把当前配置存成预设")},
    };
    return kList;
}

/// 按**锚点**取章节标题。
///
/// 刻意不用下标：在中间插入一章时，文本版与 HTML 版的下标会各自错位成
/// 「标题配错正文」—— 这种事编译不报错，看输出也未必发现。锚点写错只会
/// 得到空标题，测试能立刻抓住。
[[nodiscard]] QString section_title(QStringView anchor) {
    for (const CheatSection& section : sections()) {
        if (section.anchor == anchor) {
            return section.title;
        }
    }
    return {};
}

}  // namespace

QList<HelperDoc> helper_docs() {
    // 顺序与 helper::registered_names() 一致
    // （列表 → 组合 → 生成 → 文本 → 路径 → 文件名 → 类型）
    return {
            // ---- 列表 ----
            {QStringLiteral("列表"),
             {QStringLiteral("count")},
             QStringLiteral("count(list)"),
             QStringLiteral("长度，等价于 #list")},
            {QStringLiteral("列表"),
             {QStringLiteral("index")},
             QStringLiteral("index(list, i)"),
             QStringLiteral("第 i 项，等价于 list[i]")},
            {QStringLiteral("列表"),
             {QStringLiteral("tolist")},
             QStringLiteral("tolist(value)"),
             QStringLiteral("把单个值包成单项列表")},
            {QStringLiteral("列表"),
             {QStringLiteral("slice")},
             QStringLiteral("slice(list, from[, to])"),
             QStringLiteral("取子段，含两端；省略 to 取到末尾")},
            {QStringLiteral("列表"),
             {QStringLiteral("concat")},
             QStringLiteral("concat(list[, sep])"),
             QStringLiteral("拼成一个字符串，可给分隔符")},
            {QStringLiteral("列表"),
             {QStringLiteral("sort")},
             QStringLiteral("sort(list)"),
             QStringLiteral("字典序排序")},
            {QStringLiteral("列表"),
             {QStringLiteral("natsort")},
             QStringLiteral("natsort(list)"),
             QStringLiteral("自然序排序：file2 排在 file10 之前")},
            {QStringLiteral("列表"),
             {QStringLiteral("uniq")},
             QStringLiteral("uniq(list)"),
             QStringLiteral("去重，保留首次出现的顺序")},

            // ---- 组合 ----
            {QStringLiteral("组合"),
             {QStringLiteral("matrix")},
             QStringLiteral("matrix(left, right[, sep])"),
             QStringLiteral("笛卡尔积，sep 默认 \"-\"，左为外层右为内层")},
            {QStringLiteral("组合"),
             {QStringLiteral("zip")},
             QStringLiteral("zip(left, right[, sep])"),
             QStringLiteral("逐项配对（短的一侧按空串补位）")},

            // ---- 生成 ----
            {QStringLiteral("生成"),
             {QStringLiteral("seq")},
             QStringLiteral("seq(n) / seq(from, to[, step])"),
             QStringLiteral("等差数列，元素是字符串；step 可为负")},
            {QStringLiteral("生成"),
             {QStringLiteral("rand")},
             QStringLiteral("rand() / rand(n) / rand(a, b)"),
             QStringLiteral("随机数；固定种子下结果可复现")},
            {QStringLiteral("生成"),
             {QStringLiteral("seed")},
             QStringLiteral("seed(n)"),
             QStringLiteral("设定随机种子，让预览与实际执行一致")},
            {QStringLiteral("生成"),
             {QStringLiteral("regex")},
             QStringLiteral("regex(s, pattern[, n])"),
             QStringLiteral("正则捕获列表：有捕获组给各组，无则给整个匹配；给了 n 直接取第 n 项")},

            // ---- 文本 ----
            {QStringLiteral("文本"),
             {QStringLiteral("fmt")},
             QStringLiteral("fmt(fmt, ...)"),
             QStringLiteral("同 string.format，如 fmt(\"%03d\", n)")},
            {QStringLiteral("文本"),
             {QStringLiteral("upper"), QStringLiteral("lower")},
             QStringLiteral("upper(s) / lower(s)"),
             QStringLiteral("转大小写")},
            {QStringLiteral("文本"),
             {QStringLiteral("trim")},
             QStringLiteral("trim(s)"),
             QStringLiteral("去掉首尾空白")},
            {QStringLiteral("文本"),
             {QStringLiteral("replace")},
             QStringLiteral("replace(s, find, to)"),
             QStringLiteral("**按字面**全局替换（不认正则；要正则用 resub）")},
            {QStringLiteral("文本"),
             {QStringLiteral("resub")},
             QStringLiteral("resub(s, pattern, to)"),
             QStringLiteral("正则全局替换，可用 \\1…\\9 引用捕获组")},
            {QStringLiteral("文本"),
             {QStringLiteral("match")},
             QStringLiteral("match(s, pattern)"),
             QStringLiteral("首个匹配串，没有则空串")},
            {QStringLiteral("文本"),
             {QStringLiteral("pad")},
             QStringLiteral("pad(s, width[, fill])"),
             QStringLiteral("右侧补齐到 width 个字符，fill 默认空格")},

            // ---- 路径 ----
            {QStringLiteral("路径"),
             {QStringLiteral("basename"), QStringLiteral("dirname")},
             QStringLiteral("basename(p) / dirname(p)"),
             QStringLiteral("文件名 / 所在目录")},
            {QStringLiteral("路径"),
             {QStringLiteral("ext"), QStringLiteral("stem")},
             QStringLiteral("ext(p) / stem(p)"),
             QStringLiteral("扩展名（不含点）/ 去扩展名的文件名")},
            {QStringLiteral("路径"),
             {QStringLiteral("join")},
             QStringLiteral("join(part...)"),
             QStringLiteral("按 / 拼接路径，自动处理重复斜杠")},

            // ---- 文件名 ----
            {QStringLiteral("文件名"),
             {QStringLiteral("set_ext")},
             QStringLiteral("set_ext(p, e)"),
             QStringLiteral("换扩展名（替换）；e 可带点，给空串等于去掉扩展名")},
            {QStringLiteral("文件名"),
             {QStringLiteral("add_ext")},
             QStringLiteral("add_ext(p, e)"),
             QStringLiteral("在末尾追加一层扩展名：a.mkv → a.mkv.bak")},
            {QStringLiteral("文件名"),
             {QStringLiteral("add_suffix")},
             QStringLiteral("add_suffix(p, s)"),
             QStringLiteral("插在扩展名之前：a.mkv → a_final.mkv（加标记用这个）")},
            {QStringLiteral("文件名"),
             {QStringLiteral("safe_name")},
             QStringLiteral("safe_name(s[, repl])"),
             QStringLiteral("变成能落盘的文件名：换非法字符、去结尾点与空格、躲开设备名")},

            // ---- 类型 ----
            {QStringLiteral("类型"),
             {QStringLiteral("num")},
             QStringLiteral("num(s)"),
             QStringLiteral("转成数字供运算；解析不出来则为空串")},
            {QStringLiteral("类型"),
             {QStringLiteral("str")},
             QStringLiteral("str(s)"),
             QStringLiteral("转成字符串，如 str(1 + 1)")},
    };
}

QList<CheatExample> cheat_examples() {
    const QStringList l1{QStringLiteral("报告A"), QStringLiteral("报告B"), QStringLiteral("报告C")};
    const QStringList l2{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    const QStringList none{};

    return {
            // ------------------------------------------------------------------
            // 基本形态
            // ------------------------------------------------------------------
            {kGroupSyntax,
             QStringLiteral("字面文本与区段混写；区段里的 Lua 求值后替换该处"),
             QStringLiteral(R"dsl(mv $list1[i]$ out/$list2[i]$)dsl"),
             l1,
             l2,
             {QStringLiteral("mv 报告A out/1"),
              QStringLiteral("mv 报告B out/2"),
              QStringLiteral("mv 报告C out/3")}},

            {kGroupSyntax,
             QStringLiteral("下标固定取值（写法里没有 i，所以只出一行）"),
             QStringLiteral(R"dsl($list1[2]$ 的编号是 $list2[2]$)dsl"),
             l1,
             l2,
             {QStringLiteral("报告B 的编号是 2")}},

            {kGroupSyntax,
             QStringLiteral("区段可以连续多个，字面文本原样保留"),
             QStringLiteral(R"dsl([$list1[i]$]-[$list2[i]$])dsl"),
             l1,
             l2,
             {QStringLiteral("[报告A]-[1]"),
              QStringLiteral("[报告B]-[2]"),
              QStringLiteral("[报告C]-[3]")}},

            // ------------------------------------------------------------------
            // 行与列表
            // ------------------------------------------------------------------
            {kGroupRows,
             QStringLiteral("i 是当前行号（从 1 起），rows 是总行数"),
             QStringLiteral(R"dsl(第 $i$/$rows$ 行：$list1[i]$)dsl"),
             l1,
             l2,
             {QStringLiteral("第 1/3 行：报告A"),
              QStringLiteral("第 2/3 行：报告B"),
              QStringLiteral("第 3/3 行：报告C")}},

            {kGroupRows,
             QStringLiteral("列表长度：$#list$ 与 count() 等价"),
             QStringLiteral(R"dsl($#list1$ = $count(list1)$)dsl"),
             l1,
             l2,
             {QStringLiteral("3 = 3")}},

            {kGroupRows,
             QStringLiteral("只用一个列表也可以；行数由它决定"),
             QStringLiteral(R"dsl(mkdir -p out/$list1[i]$)dsl"),
             l1,
             none,
             {QStringLiteral("mkdir -p out/报告A"),
              QStringLiteral("mkdir -p out/报告B"),
              QStringLiteral("mkdir -p out/报告C")}},

            {kGroupRows,
             QStringLiteral("两个列表长度不同时，行数取最长，短的按空串补位"),
             QStringLiteral(R"dsl($list1[i]$|$list2[i]$)dsl"),
             {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
             {QStringLiteral("1"), QStringLiteral("2")},
             {QStringLiteral("a|1"), QStringLiteral("b|2"), QStringLiteral("c|")}},

            {kGroupRows,
             QStringLiteral("越界索引返回空串，不会报错"),
             QStringLiteral(R"dsl([$list1[99]$])dsl"),
             l1,
             none,
             {QStringLiteral("[]")}},

            // ------------------------------------------------------------------
            // 组合与展开
            // ------------------------------------------------------------------
            {kGroupExpand,
             QStringLiteral("matrix 求笛卡尔积：3×3 = 9 行（整个区段展开成多行）"),
             QStringLiteral(R"dsl($matrix(list1, list2, "-")$)dsl"),
             l1,
             l2,
             {QStringLiteral("报告A-1"),
              QStringLiteral("报告A-2"),
              QStringLiteral("报告A-3"),
              QStringLiteral("报告B-1"),
              QStringLiteral("报告B-2"),
              QStringLiteral("报告B-3"),
              QStringLiteral("报告C-1"),
              QStringLiteral("报告C-2"),
              QStringLiteral("报告C-3")}},

            {kGroupExpand,
             QStringLiteral("配上一行一个下标，就变成「逐行对应」（这里只取前 3 个组合）"),
             QStringLiteral(R"dsl(组合 $i$：$matrix(list1, list2, "-")[i]$)dsl"),
             l1,
             l2,
             {QStringLiteral("组合 1：报告A-1"),
              QStringLiteral("组合 2：报告A-2"),
              QStringLiteral("组合 3：报告A-3")}},

            {kGroupExpand,
             QStringLiteral("zip 逐项配对；要想逐行对应又不想写下标就用它"),
             QStringLiteral(R"dsl($zip(list1, list2, "→")$)dsl"),
             l1,
             l2,
             {QStringLiteral("报告A→1"), QStringLiteral("报告B→2"), QStringLiteral("报告C→3")}},

            {kGroupExpand,
             QStringLiteral("返回列表的区段会自动展开成多行"),
             QStringLiteral(R"dsl($slice(list1, 2, 3)$)dsl"),
             l1,
             none,
             {QStringLiteral("报告B"), QStringLiteral("报告C")}},

            // ------------------------------------------------------------------
            // 工具函数
            // ------------------------------------------------------------------
            {kGroupHelper,
             QStringLiteral("补零编号：fmt + num"),
             QStringLiteral(R"dsl($fmt("%03d", num(list2[i]))$-$list1[i]$)dsl"),
             l1,
             l2,
             {QStringLiteral("001-报告A"),
              QStringLiteral("002-报告B"),
              QStringLiteral("003-报告C")}},

            {kGroupHelper,
             QStringLiteral("右侧补齐到指定宽度"),
             QStringLiteral(R"dsl([$pad(list1[i], 6, ".")$])dsl"),
             l1,
             none,
             {QStringLiteral("[报告A...]"),
              QStringLiteral("[报告B...]"),
              QStringLiteral("[报告C...]")}},

            {kGroupHelper,
             QStringLiteral("拼接列表；分隔符可省略"),
             QStringLiteral(R"dsl($concat(list1, "、")$)dsl"),
             l1,
             none,
             {QStringLiteral("报告A、报告B、报告C")}},

            {kGroupHelper,
             QStringLiteral("自然序排序：file2 排在 file10 之前（资源管理器那样）"),
             QStringLiteral(R"dsl($concat(natsort({"file10", "file2", "file1"}), " ")$)dsl"),
             none,
             none,
             {QStringLiteral("file1 file2 file10")}},

            {kGroupHelper,
             QStringLiteral("正则捕获组：用 [[...]] 写 pattern，不必转义反斜杠"),
             QStringLiteral(R"dsl($regex("2026-09-15", [[(\d+)-(\d+)-(\d+)]])[1]$)dsl"),
             none,
             none,
             {QStringLiteral("2026")}},

            {kGroupHelper,
             QStringLiteral("正则直接取第 n 项：省掉下标，越界给空串"),
             QStringLiteral(R"dsl($regex("番剧 第07话", [[第(\d+)话]], 1)$)dsl"),
             none,
             none,
             {QStringLiteral("07")}},

            {kGroupHelper,
             QStringLiteral("字面替换（不认正则）：`.` 就是点本身"),
             QStringLiteral(R"dsl($replace("2026.09.15", ".", "-")$)dsl"),
             none,
             none,
             {QStringLiteral("2026-09-15")}},

            {kGroupHelper,
             QStringLiteral("正则替换：可用 \\1 引用捕获组"),
             QStringLiteral(R"dsl($resub("番剧 第07话", [[第(\d+)话]], [[第\1话 正片]])$)dsl"),
             none,
             none,
             {QStringLiteral("番剧 第07话 正片")}},

            {kGroupHelper,
             QStringLiteral("取文件名主干（扩展名去掉）"),
             QStringLiteral(R"dsl($stem(basename("D:/in/IMG_0001.jpeg"))$)dsl"),
             none,
             none,
             {QStringLiteral("IMG_0001")}},

            {kGroupHelper,
             QStringLiteral("拼路径；自动处理斜杠"),
             QStringLiteral(R"dsl($join("out", list1[i], list2[i] .. ".txt")$)dsl"),
             l1,
             l2,
             {QStringLiteral("out/报告A/1.txt"),
              QStringLiteral("out/报告B/2.txt"),
              QStringLiteral("out/报告C/3.txt")}},

            {kGroupHelper,
             QStringLiteral("数列取值：第 i 个数"),
             QStringLiteral(R"dsl(编号 $seq(5)[i]$)dsl"),
             l1,
             none,
             {QStringLiteral("编号 1"), QStringLiteral("编号 2"), QStringLiteral("编号 3")}},

            {kGroupHelper,
             QStringLiteral("去重"),
             QStringLiteral(R"dsl($concat(uniq({"a", "b", "a", "c"}), "")$)dsl"),
             none,
             none,
             {QStringLiteral("abc")}},

            {kGroupHelper,
             QStringLiteral("删掉一段固定文本（字面替换成空串，全部出现）"),
             QStringLiteral(R"dsl($replace("a_final_final.txt", "_final", "")$)dsl"),
             none,
             none,
             {QStringLiteral("a.txt")}},

            {kGroupHelper,
             QStringLiteral("随机：固定种子下每次预览都一样，不会「预览与执行不同」"),
             QStringLiteral(R"dsl($rand(1, 100)$)dsl"),
             none,
             none,
             {}},

            {kGroupHelper,
             QStringLiteral("换扩展名：只动最后一个点之后的部分，目录不动"),
             QStringLiteral(R"dsl($set_ext("D:/in/IMG_0001.jpeg", "jpg")$)dsl"),
             none,
             none,
             {QStringLiteral("D:/in/IMG_0001.jpg")}},

            {kGroupHelper,
             QStringLiteral("本来没有扩展名就直接接上，不会多出一个点"),
             QStringLiteral(R"dsl($set_ext("片子 第01话", "mkv")$)dsl"),
             none,
             none,
             {QStringLiteral("片子 第01话.mkv")}},

            {kGroupHelper,
             QStringLiteral("第二参数给空串等于去掉扩展名"),
             QStringLiteral(R"dsl($set_ext("out/片子 第01话.mkv", "")$)dsl"),
             none,
             none,
             {QStringLiteral("out/片子 第01话")}},

            {kGroupHelper,
             QStringLiteral("追加一层扩展名：备份那种「加在最后」的用法"),
             QStringLiteral(R"dsl($add_ext("片子 第01话.mkv", "bak")$)dsl"),
             none,
             none,
             {QStringLiteral("片子 第01话.mkv.bak")}},

            {kGroupHelper,
             QStringLiteral("在扩展名之前插一段：给一整批加同一个标记"),
             QStringLiteral(R"dsl($add_suffix(list1[i], "_终稿")$)dsl"),
             l1,
             none,
             {QStringLiteral("报告A_终稿"),
              QStringLiteral("报告B_终稿"),
              QStringLiteral("报告C_终稿")}},

            {kGroupHelper,
             QStringLiteral("文件名安全化：换掉冒号这类非法字符（Linux 上能建、Windows 上不能）"),
             QStringLiteral(R"dsl($safe_name("第1话 序章: 起点")$)dsl"),
             none,
             none,
             {QStringLiteral("第1话 序章_ 起点")}},

            {kGroupHelper,
             QStringLiteral(
                     "安全化会去掉结尾的点与空格、给设备名补下划线 —— 这三件事不做都会静默出错"),
             QStringLiteral(R"dsl([$safe_name("a. ")$] [$safe_name("con.mkv")$])dsl"),
             none,
             none,
             {QStringLiteral("[a] [con_.mkv]")}},

            // ------------------------------------------------------------------
            // Lua 原生
            // ------------------------------------------------------------------
            {kGroupLua,
             QStringLiteral("区段里就是 Lua，四则运算直接写"),
             QStringLiteral(R"dsl($(num(list2[i]) + 10)$)dsl"),
             none,
             l2,
             {QStringLiteral("11"), QStringLiteral("12"), QStringLiteral("13")}},

            {kGroupLua,
             QStringLiteral("整除 // 与真除 / 的结果不同（Lua 5.4 规则）"),
             QStringLiteral(R"dsl($(7 // 2)$ 与 $(7 / 2)$)dsl"),
             none,
             none,
             {QStringLiteral("3 与 3.5")}},

            {kGroupLua,
             QStringLiteral("条件表达式（Lua 的三目写法）"),
             QStringLiteral(R"dsl($(i % 2 == 1 and "奇数" or "偶数")$)dsl"),
             l1,
             none,
             {QStringLiteral("奇数"), QStringLiteral("偶数"), QStringLiteral("奇数")}},

            {kGroupLua,
             QStringLiteral("白名单内的标准库可直接用：string / table / math / utf8"),
             QStringLiteral(R"dsl($table.concat({list1[i], list2[i]}, "_")$)dsl"),
             l1,
             l2,
             {QStringLiteral("报告A_1"), QStringLiteral("报告B_2"), QStringLiteral("报告C_3")}},

            {kGroupLua,
             QStringLiteral("字符串方法同样可用"),
             QStringLiteral(R"dsl($("abc"):upper()$)dsl"),
             none,
             none,
             {QStringLiteral("ABC")}},

            // ------------------------------------------------------------------
            // 转义与边界
            // ------------------------------------------------------------------
            {kGroupEdge,
             QStringLiteral("字面段里的 \\$ 表示一个普通美元符号"),
             QStringLiteral(R"dsl(应付：\$100)dsl"),
             none,
             none,
             {QStringLiteral("应付：$100")}},

            {kGroupEdge,
             QStringLiteral("区段内要写字面 $ 时，在引号里写 \\$"),
             QStringLiteral(R"dsl(单价 $"\$"$ $list2[i]$ 元)dsl"),
             none,
             l2,
             {QStringLiteral("单价 $ 1 元"),
              QStringLiteral("单价 $ 2 元"),
              QStringLiteral("单价 $ 3 元")}},

            {kGroupEdge,
             QStringLiteral("整批不需要行上下文时，模板只求值一次（所以这里只有一行）"),
             QStringLiteral(R"dsl(固定的一行文本)dsl"),
             l1,
             l2,
             {QStringLiteral("固定的一行文本")}},
    };
}

QString cheatsheet_text() {
    QStringList lines;

    lines.append(QStringLiteral("BatchSmith DSL 速查表  %1")
                         .arg(QString::fromLatin1(batchsmith::core::version_string())));
    lines.append(QString(72, QLatin1Char('=')));
    lines.append(QString());

    // ---- 基本形态 ----
    lines.append(section_title(u"sec-overview"));
    lines.append(QString());
    lines.append(QStringLiteral("    模板 = 普通文字 + 若干 $...$ 区段。"));
    lines.append(QStringLiteral("    $ 与 $ 之间写一段 Lua 表达式，求值结果替换这一处。"));
    lines.append(QString());
    lines.append(QStringLiteral("        输入   mv $list1[i]$ out/$list2[i]$"));
    lines.append(QStringLiteral("        输出   mv 报告A out/1"));
    lines.append(QStringLiteral("               mv 报告B out/2"));
    lines.append(QStringLiteral("               mv 报告C out/3"));
    lines.append(QString());

    // ---- 可用的名字 ----
    lines.append(section_title(u"sec-names"));
    lines.append(QString());
    lines.append(QStringLiteral("    i              当前行号，从 1 开始"));
    lines.append(QStringLiteral("    rows           本批总行数"));
    lines.append(QStringLiteral("    list1、list2…  上面列表区的各列，下标从 1 开始"));
    lines.append(QStringLiteral("    $#list1$       列表长度（等价 count(list1)）"));
    lines.append(QString());
    lines.append(QStringLiteral("    越界下标给空串，不报错；"));
    lines.append(
            QStringLiteral("    名字写成 listN 却没这个列表时会明确报错（打错字不会被忽略）。"));
    lines.append(QString());

    // ---- 列表从哪来 ----
    lines.append(section_title(u"sec-lists"));
    lines.append(QString());
    lines.append(QStringLiteral("    · 每列可以在两种来源之间切换（列标题旁的下拉，"
                                "或者直接把文件夹拖到列上）："));
    lines.append(QStringLiteral("        手输      直接键入条目"));
    lines.append(QStringLiteral("        文件夹    绑定一个文件夹，按过滤取数"));
    lines.append(QStringLiteral("    · 文件夹来源的条目是相对该文件夹的路径，用 / 分隔，按自然序"));
    lines.append(
            QStringLiteral("      排列（第2话 在第10话 之前）。要绝对路径就 join(根, list1[i])。"));
    lines.append(QStringLiteral("    · 过滤写 glob：* 任意长度、? 恰好一个字符、[abc] 字符集"));
    lines.append(QStringLiteral("      （[!abc] 取反）；大小写不敏感，只匹配文件名、不跨目录；"));
    lines.append(QStringLiteral("      多个用 ; 或 , 分隔（任一匹配即可）。"));
    lines.append(QStringLiteral("    · 路径不通会明确报错，而不是给一个空列表 —— 空列表在批量"));
    lines.append(QStringLiteral("      操作里意味着「什么都不会发生」，是最难排查的一类问题。"));
    lines.append(
            QStringLiteral("    · 命令行：--list 'list1=@dir:D:/anime;filter=*.mkv;recursive=1'"));
    lines.append(
            QStringLiteral("      另有 dirs=1（把子目录也算条目）、hidden=1（含 . 开头的条目）。"));
    lines.append(QStringLiteral("    · 各列长度不齐时，每列有自己的缺省：留空 / 忽略（取最短）/"));
    lines.append(QStringLiteral("      从头重复。命令行用 --ignore 名 把某列设为「忽略」。"));
    lines.append(QString());

    // ---- 行数与展开 ----
    lines.append(section_title(u"sec-rows"));
    lines.append(QString());
    lines.append(QStringLiteral("    · 行数 = 各列表长度：有 Ignore 参与的取最短，否则取最长。"));
    lines.append(QStringLiteral("    · 模板里用了 i 或 rows ⇒ 逐行求值（这就是「逐行对应」）。"));
    lines.append(
            QStringLiteral("    · 没用 i / rows ⇒ 整批只求值一次，适合 matrix 这类整体展开。"));
    lines.append(QStringLiteral("    · 某个区段求值得到一个列表 ⇒ 该行展开成多行；"));
    lines.append(
            QStringLiteral("      多个列表区段取笛卡尔积（左外层右内层），字面文本随展开复制。"));
    lines.append(QString());

    // ---- 工具函数 ----
    lines.append(section_title(u"sec-helpers"));
    lines.append(QString());
    QString current_group;
    for (const HelperDoc& doc : helper_docs()) {
        if (doc.group != current_group) {
            current_group = doc.group;
            lines.append(QStringLiteral("    [%1]").arg(current_group));
        }
        lines.append(
                QStringLiteral("      %1  %2").arg(doc.signature.leftJustified(34), doc.summary));
    }
    lines.append(QString());

    // ---- Lua 原生 ----
    lines.append(section_title(u"sec-lua"));
    lines.append(QString());
    lines.append(QStringLiteral("    可用：算术与比较、字符串（含 s:upper() 这类方法）、"));
    lines.append(QStringLiteral("          string / table / math / utf8 四张库，"));
    lines.append(QStringLiteral("          以及 type tostring tonumber pairs ipairs select assert "
                                "error pcall 等基础函数。"));
    lines.append(QString());
    lines.append(
            QStringLiteral("    不可用：io os package require load dofile debug coroutine print"));
    lines.append(QStringLiteral(
            "            collectgarbage setmetatable _G（需要碰文件系统的操作不在这里做）。"));
    lines.append(QString());
    lines.append(QStringLiteral(
            "    另有三重保险：指令数、内存、执行时长都有上限，写错成死循环会被中止。"));
    lines.append(QString());

    // ---- 易错点 ----
    lines.append(section_title(u"sec-pitfalls"));
    lines.append(QString());
    lines.append(QStringLiteral("    · 区段里的字符串是 Lua 字符串，反斜杠要按 Lua 规则写："));
    lines.append(QStringLiteral("      正则推荐用长括号 [[\\d+]]，不必转义。"));
    lines.append(QStringLiteral("    · 区段里不能出现光秃秃的 $；要字面美元符就在引号里写 \\$。"));
    lines.append(QStringLiteral("    · / 与 ^ 的结果是浮点数（Lua 5.4）：$(4 / 2)$ 是 2.0。"));
    lines.append(QStringLiteral("      要整数用 //。"));
    lines.append(
            QStringLiteral("    · 除 listN 之外，读一个不存在的名字只会得到空串，不会报错 ——"));
    lines.append(QStringLiteral("      输出意外变空时先检查名字是否拼错。"));
    lines.append(QString());

    // ---- 示例 ----
    lines.append(section_title(u"sec-examples"));
    lines.append(QString());

    QString example_group;
    for (const CheatExample& example : cheat_examples()) {
        if (example.group != example_group) {
            example_group = example.group;
            lines.append(QStringLiteral("    ── %1 ──").arg(example_group));
        }
        lines.append(QStringLiteral("    %1").arg(example.title));
        lines.append(QStringLiteral("        %1").arg(example.expression));
        if (example.expect.isEmpty()) {
            lines.append(QStringLiteral("        → （每次预览结果一致）"));
        } else {
            for (const QString& row : example.expect) {
                lines.append(QStringLiteral("        → %1").arg(row));
            }
            if (example.expect.size() > 1) {
                lines.append(QStringLiteral("        （共 %1 行）").arg(example.expect.size()));
            }
        }
        lines.append(QString());
    }

    lines.append(QStringLiteral("    列表区的值就是上面示例里的 list1 = 报告A、报告B、报告C，"));
    lines.append(QStringLiteral("    list2 = 1、2、3。"));
    lines.append(QString());

    // ---- 预设 ----
    lines.append(section_title(u"sec-preset"));
    lines.append(QString());
    lines.append(QStringLiteral("    · 预设 = 当前的列表 + 表达式，存成一个 .toml 文件。"));
    lines.append(QStringLiteral("      · 存：「文件 → 保存预设(Ctrl+S)」不用先挑路径 —— 没存过的"));
    lines.append(
            QStringLiteral("        直接落进预设目录（重名自动加序号）；要挑地方用「另存为」。"));
    lines.append(
            QStringLiteral("      · 取：「预设」菜单里直接列着最近用过的，点一下就切过去（当前"));
    lines.append(QStringLiteral("        那个带勾）；也可以用「文件 → 打开预设(Ctrl+O)」。"));
    lines.append(
            QStringLiteral("      · 想让某套预设「双击即用」：在「预设 → 管理预设」里选中它，点"));
    lines.append(
            QStringLiteral("        「创建快捷方式…」，挑一个目录即可（那里也能给它挑个图标）。"));
    lines.append(QStringLiteral("      · 启动时直接带上预设也可以："));
    lines.append(QStringLiteral("          batchsmith 我的预设.toml"));
    lines.append(QStringLiteral("          batchsmith --preset \"我的预设.toml\""));
    lines.append(QString());
    lines.append(QStringLiteral(
            "    · 备注：预设里可以写一行 note = '...'（「管理预设 → 备注…」改的就是"));
    lines.append(QStringLiteral("      它）。它是给人看的一句话，会跟着预设一起分享出去。"));
    lines.append(QStringLiteral(
            "    · 图标：管理预设里能给预设挑一个图标，建快捷方式时用它。图标是「本机"));
    lines.append(QStringLiteral(
            "      路径」，所以写在旁边的 .local.toml 里、不进预设文件 —— 预设照样"));
    lines.append(QStringLiteral("      可以干净地发给别人。"));
    lines.append(QString());
    lines.append(QStringLiteral("    · 预设里不存绝对路径：绑定的文件夹写成槽位 ${input}，"));
    lines.append(QStringLiteral("      本机实际路径存在旁边的 <预设名>.local.toml 里。"));
    lines.append(
            QStringLiteral("      于是预设文件可以进版本控制、可以发给别人 —— 分享时别带那个"));
    lines.append(QStringLiteral("      .local.toml。"));
    lines.append(QString());
    lines.append(
            QStringLiteral("    · 槽位还没绑定时，加载后会明确说出来；把文件夹拖到那一列上就"));
    lines.append(QStringLiteral("      会自动绑好，保存时记住。"));
    lines.append(QString());
    lines.append(
            QStringLiteral("    · 文件长这样（界面另存为写出的就是这个形状，可以直接手改）："));
    lines.append(QString());
    lines.append(QStringLiteral("        [preset]"));
    lines.append(QStringLiteral("        name = '番剧重命名'"));
    lines.append(QStringLiteral("        version = 1"));
    lines.append(
            QStringLiteral("        note = '只留 mkv'          # 可省（管理预设里的「备注…」）"));
    lines.append(QString());
    lines.append(QStringLiteral("        [[lists]]"));
    lines.append(QStringLiteral("        id = 'list1'"));
    lines.append(QStringLiteral("        source = { kind = 'dir', path = '${input}', "
                                "filter = '*.mkv' }"));
    lines.append(QStringLiteral("        fill = 'empty'"));
    lines.append(QString());
    lines.append(QStringLiteral("        [[lists]]"));
    lines.append(QStringLiteral("        id = 'list2'"));
    lines.append(QStringLiteral("        items = ['第01话', '第02话']    # 不写 source 就是手输"));
    lines.append(QStringLiteral("        fill = 'empty'"));
    lines.append(QString());
    lines.append(QStringLiteral("        [output]"));
    lines.append(QStringLiteral("        mode = 'rename'"));
    lines.append(QStringLiteral("        template = '$stem(list1[i])$ $list2[i]$.mkv'"));
    lines.append(QString());
    lines.append(QStringLiteral("    · fill 决定长度不齐时怎么办：empty 留空 / ignore 取最短 /"));
    lines.append(QStringLiteral("      repeat 从头重复。"));
    lines.append(
            QStringLiteral("    · 还没实现的来源（expr / snapshot / seq / rand…）写进 kind 会"));
    lines.append(QStringLiteral("      明确报错，不会静默忽略。"));

    return lines.join(QLatin1Char('\n'));
}

QList<CheatSection> cheatsheet_sections() {
    return sections();
}

}  // namespace batchsmith::core::dsl
