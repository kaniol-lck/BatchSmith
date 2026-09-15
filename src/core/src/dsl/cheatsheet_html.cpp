#include "batchsmith/core/dsl/cheatsheet.hpp"

#include <iterator>

#include "batchsmith/core/version.hpp"

namespace batchsmith::core::dsl {
namespace {

/// 转义成 HTML 文本。
///
/// 速查表里大量出现 `$...$`、`<`、`&`（示例表达式、`[[\d+]]`、`s:upper()` 等），
/// 不转义就会被富文本引擎当成标记 —— 轻则显示错乱，重则整段被吃掉。
[[nodiscard]] QString esc(const QString& text) {
    return text.toHtmlEscaped();
}

[[nodiscard]] QString style_sheet() {
    // 只用 Qt 富文本引擎支持的基础属性（不用 flex/grid/border-collapse 一类），
    // 否则在 QTextBrowser 里会被静默忽略。
    return QStringLiteral("body { margin: 14px 16px; }\n"
                          "h1 { font-size: 15pt; margin: 0 0 2px 0; }\n"
                          "h2 { font-size: 12pt; margin: 18px 0 6px 0; padding-bottom: 3px;"
                          " border-bottom: 1px solid #d0d0d0; }\n"
                          "h3 { font-size: 11pt; margin: 14px 0 4px 0; padding: 2px 6px;"
                          " background: #f0f0f0; }\n"
                          "h4 { font-size: 10.5pt; margin: 10px 0 2px 0; }\n"
                          "p, li { line-height: 145%; }\n"
                          "code { font-family: monospace; background: #f2f2f2; }\n"
                          "pre { font-family: monospace; background: #f7f7f7;"
                          " border: 1px solid #e0e0e0; padding: 6px 8px; margin: 3px 0 10px 0; }\n"
                          "table { margin: 4px 0 10px 0; }\n"
                          "th, td { border: 1px solid #d0d0d0; padding: 3px 8px; text-align: left;"
                          " vertical-align: top; }\n"
                          "th { background: #f0f0f0; }\n"
                          ".toc { background: #f8f8f8; border: 1px solid #e0e0e0;"
                          " padding: 8px 12px; margin: 10px 0 4px 0; }\n"
                          ".toc ul { margin: 4px 0 0 0; padding-left: 22px; }\n"
                          ".ver { color: #666666; margin: 0 0 6px 0; }\n"
                          ".note { color: #666666; }\n");
}

/// 一个分组的锚点（HTML 里的 id 与 CHM 目录里的跳转目标都用它）
struct GroupAnchor {
    QString id;
    QString title;
};

/// 按数据顺序取"去重后的分组名"
[[nodiscard]] QStringList unique_helper_groups() {
    QStringList groups;
    for (const HelperDoc& doc : helper_docs()) {
        if (!groups.contains(doc.group)) {
            groups.append(doc.group);
        }
    }
    return groups;
}

[[nodiscard]] QStringList unique_example_groups() {
    QStringList groups;
    for (const CheatExample& example : cheat_examples()) {
        if (!groups.contains(example.group)) {
            groups.append(example.group);
        }
    }
    return groups;
}

/// 生成分组锚点。**这是分组锚点的唯一定义处** ——
/// HTML 渲染（点击目录能跳到分组）与 CHM 的 .hhc（左侧树的二级节点）都取它，
/// 两边各写一份的话迟早对不上，而"点了跳不过去"是很难被测出来的那类问题。
[[nodiscard]] QList<GroupAnchor> group_anchors(const QStringList& groups, const QString& prefix) {
    QList<GroupAnchor> result;
    result.reserve(groups.size());
    for (qsizetype index = 0; index < groups.size(); ++index) {
        result.append({prefix + QString::number(index), groups.at(index)});
    }
    return result;
}

/// 章节 1：模板长什么样
[[nodiscard]] QString render_overview() {
    return QStringLiteral("<p>模板 = 普通文字 + 若干 <code>$...$</code> 区段。"
                          "<code>$</code> 与 <code>$</code> 之间写一段 Lua 表达式，"
                          "求值结果替换这一处。</p>"
                          "<pre>输入   mv $list1[i]$ out/$list2[i]$\n"
                          "输出   mv 报告A out/1\n"
                          "       mv 报告B out/2\n"
                          "       mv 报告C out/3</pre>");
}

/// 章节 2：可用的名字
[[nodiscard]] QString render_names() {
    return QStringLiteral("<table>"
                          "<tr><th>名字</th><th>含义</th></tr>"
                          "<tr><td><code>i</code></td><td>当前行号，从 1 开始</td></tr>"
                          "<tr><td><code>rows</code></td><td>本批总行数</td></tr>"
                          "<tr><td><code>list1</code>、<code>list2</code>…</td>"
                          "<td>上面列表区的各列，下标从 1 开始</td></tr>"
                          "<tr><td><code>$#list1$</code></td>"
                          "<td>列表长度（等价 <code>count(list1)</code>）</td></tr>"
                          "</table>"
                          "<p>越界下标给空串，不报错；名字写成 <code>listN</code> 却没有这个列表时"
                          "会明确报错（打错字不会被忽略）。</p>");
}

/// 章节 3：列表从哪来
[[nodiscard]] QString render_lists() {
    return QStringLiteral(
            "<p>每一列都可以在两种来源之间切换（列标题旁的下拉，"
            "或者直接把文件夹拖到列上）：</p>"
            "<table>"
            "<tr><th>来源</th><th>说明</th></tr>"
            "<tr><td><b>手输</b></td><td>直接键入条目，可增删改</td></tr>"
            "<tr><td><b>文件夹</b></td><td>绑定一个文件夹，按过滤与递归开关取数"
            "（列表只读，想手改就切回手输）</td></tr>"
            "</table>"
            "<ul>"
            "<li>文件夹来源的条目是<b>相对该文件夹</b>的路径，用 <code>/</code> 分隔，"
            "按<b>自然序</b>排列（<code>第2话</code> 在 <code>第10话</code> 之前）。"
            "要绝对路径就 <code>join(根, list1[i])</code>。</li>"
            "<li>过滤写 glob：<code>*</code> 任意长度、<code>?</code> 恰好一个字符、"
            "<code>[abc]</code> 字符集（<code>[!abc]</code> 取反）；"
            "<b>大小写不敏感</b>，只匹配文件名、不跨目录；多个用 <code>;</code> 或 "
            "<code>,</code> 分隔（任一匹配即可）。</li>"
            "<li>路径不通会<b>明确报错</b>，而不是给一个空列表 —— "
            "空列表在批量操作里意味着「什么都不会发生」，是最难排查的一类问题。</li>"
            "<li>命令行：<code>--list 'list1=@dir:D:/anime;filter=*.mkv;recursive=1'</code>；"
            "另有 <code>dirs=1</code>（把子目录也算条目）、"
            "<code>hidden=1</code>（含 <code>.</code> 开头的条目）。</li>"
            "<li>各列长度不齐时每列有自己的缺省：留空 / 忽略（取最短）/ 从头重复。"
            "命令行用 <code>--ignore 名</code> 把某列设为「忽略」。</li>"
            "</ul>");
}

/// 章节 4：行数与展开
[[nodiscard]] QString render_rows() {
    return QStringLiteral(
            "<ul>"
            "<li>行数 = 各列表长度：有 <code>Ignore</code> 参与的取最短，否则取最长。</li>"
            "<li>模板里用了 <code>i</code> 或 <code>rows</code> ⇒ <b>逐行求值</b>"
            "（这就是「逐行对应」）。</li>"
            "<li>没用 <code>i</code> / <code>rows</code> ⇒ 整批<b>只求值一次</b>，"
            "适合 <code>matrix</code> 这类整体展开。</li>"
            "<li>某个区段求值得到一个列表 ⇒ 该行展开成多行；多个列表区段取"
            "<b>笛卡尔积</b>（左外层右内层），字面文本随展开复制。</li>"
            "</ul>");
}

/// 章节 4：工具函数（按分组出表）
[[nodiscard]] QString render_helpers() {
    const QList<GroupAnchor> anchors =
            group_anchors(unique_helper_groups(), QStringLiteral("grp-"));
    QString html;
    QString current_group;
    qsizetype anchor_index = 0;
    for (const HelperDoc& doc : helper_docs()) {
        if (doc.group != current_group) {
            if (!current_group.isEmpty()) {
                html += QStringLiteral("</table>");
            }
            current_group = doc.group;
            const QString id = anchors.at(anchor_index).id;
            ++anchor_index;
            html += QStringLiteral("<h3 id=\"%1\">%2</h3><table>"
                                   "<tr><th>函数</th><th>说明</th></tr>")
                            .arg(esc(id), esc(current_group));
        }
        html += QStringLiteral("<tr><td><code>%1</code></td><td>%2</td></tr>")
                        .arg(esc(doc.signature), esc(doc.summary));
    }
    if (!current_group.isEmpty()) {
        html += QStringLiteral("</table>");
    }
    return html;
}

/// 章节 5：沙箱里可用的 Lua
[[nodiscard]] QString render_lua() {
    return QStringLiteral(
            "<p><b>可用</b>：算术与比较、字符串（含 <code>s:upper()</code> 这类方法）、"
            "<code>string</code> / <code>table</code> / <code>math</code> / "
            "<code>utf8</code> 四张库，以及 <code>type</code>、<code>tostring</code>、"
            "<code>tonumber</code>、<code>pairs</code>、<code>ipairs</code>、"
            "<code>select</code>、<code>assert</code>、<code>error</code>、"
            "<code>pcall</code> 等基础函数。</p>"
            "<p><b>不可用</b>：<code>io</code>、<code>os</code>、<code>package</code>、"
            "<code>require</code>、<code>load</code>、<code>dofile</code>、"
            "<code>debug</code>、<code>coroutine</code>、<code>print</code>、"
            "<code>collectgarbage</code>、<code>setmetatable</code>、<code>_G</code>"
            "（需要碰文件系统的操作不在这里做）。</p>"
            "<p class=\"note\">另有三重保险：指令数、内存、执行时长都有上限，"
            "写错成死循环会被中止。</p>");
}

/// 章节 6：容易踩的点
[[nodiscard]] QString render_pitfalls() {
    return QStringLiteral(
            "<ol>"
            "<li>区段里的字符串是 <b>Lua 字符串</b>，反斜杠要按 Lua 规则写 —— "
            "正则推荐用长括号 <code>[[\\d+]]</code>，不必转义。</li>"
            "<li>区段里不能出现光秃秃的 <code>$</code>；要字面美元符就在引号里写 "
            "<code>\\$</code>。</li>"
            "<li><code>/</code> 与 <code>^</code> 的结果是浮点数（Lua 5.4）："
            "<code>$(4 / 2)$</code> 是 <code>2.0</code>，要整数用 <code>//</code>。</li>"
            "<li>除 <code>listN</code> 之外，读一个不存在的名字只会得到空串、不会报错 —— "
            "输出意外变空时先检查名字是否拼错。</li>"
            "</ol>");
}

/// 章节 9：预设
[[nodiscard]] QString render_preset() {
    return QStringLiteral(
            "<p><b>预设</b> = 当前的列表 + 表达式，存成一个 <code>.toml</code> 文件。</p>"
            "<h3>怎么用</h3>"
            "<ul>"
            "<li><b>存</b>：「文件 → 保存预设」（<code>Ctrl+S</code>）<b>不用先挑路径</b> —— "
            "没存过的直接落进预设目录（重名自动加序号）；要挑地方用「另存为」。</li>"
            "<li><b>取</b>：「预设」菜单里直接列着最近用过的，点一下就切过去（当前那个带勾）；"
            "也可以用「文件 → 打开预设」（<code>Ctrl+O</code>）。</li>"
            "<li><b>双击即用</b>：在「预设 → 管理预设」里选中它，点「创建快捷方式」，"
            "桌面就会出现一个带着 <code>--preset</code> 参数的快捷方式。</li>"
            "<li>启动时直接带上预设也可以：<code>batchsmith 我的预设.toml</code> 或 "
            "<code>batchsmith --preset \"我的预设.toml\"</code>。</li>"
            "</ul>"
            "<h3>为什么不存绝对路径</h3>"
            "<p>绑定的文件夹在预设里写成<b>槽位</b> <code>${input}</code>，本机实际路径存在"
            "旁边的 <code>&lt;预设名&gt;.local.toml</code> 里。于是预设文件可以进版本控制、"
            "可以发给别人 —— 分享时别带那个 <code>.local.toml</code>。槽位还没绑定时，"
            "加载后会明确说出来；把文件夹拖到那一列上就会自动绑好，保存时记住。</p>"
            "<h3>文件长什么样</h3>"
            "<p>界面「另存为」写出的就是这个形状，可以直接手改：</p>"
            "<pre>[preset]\n"
            "name = '番剧重命名'\n"
            "version = 1\n\n"
            "[[lists]]\n"
            "id = 'list1'\n"
            "source = { kind = 'dir', path = '${input}', filter = '*.mkv' }\n"
            "fill = 'empty'\n\n"
            "[[lists]]\n"
            "id = 'list2'\n"
            "items = ['第01话', '第02话']    # 不写 source 就是手输\n"
            "fill = 'empty'\n\n"
            "[output]\n"
            "mode = 'rename'\n"
            "template = '$stem(list1[i])$ $list2[i]$.mkv'</pre>"
            "<ul>"
            "<li><code>source</code> 只管「从哪取数」：<code>kind = 'dir'</code> 绑定文件夹，"
            "不写 <code>source</code> 就是手输（<code>items</code> 就是内容）。</li>"
            "<li><code>fill</code> 管「长度不齐时怎么办」：<code>empty</code> 留空 / "
            "<code>ignore</code> 取最短 / <code>repeat</code> 从头重复。</li>"
            "<li>还没实现的来源（<code>expr</code> / <code>snapshot</code> / <code>seq</code> / "
            "<code>rand</code>…）写进 <code>kind</code> 会<b>明确报错</b>，不会静默忽略。</li>"
            "</ul>");
}

/// 章节 7：示例（每条都带实际输出）
[[nodiscard]] QString render_examples() {
    const QList<GroupAnchor> anchors =
            group_anchors(unique_example_groups(), QStringLiteral("ex-"));
    QString html;
    QString current_group;
    qsizetype anchor_index = 0;
    for (const CheatExample& example : cheat_examples()) {
        if (example.group != current_group) {
            current_group = example.group;
            const QString id = anchors.at(anchor_index).id;
            ++anchor_index;
            html += QStringLiteral("<h3 id=\"%1\">%2</h3>").arg(esc(id), esc(current_group));
        }
        html += QStringLiteral("<h4>%1</h4>").arg(esc(example.title));
        html += QStringLiteral("<pre>%1</pre>").arg(esc(example.expression));

        // 输出原样展示：把行首的 "→ " 也当成内容的一部分，避免多一层标签
        QStringList lines;
        if (example.expect.isEmpty()) {
            lines.append(QStringLiteral("（每次预览结果一致）"));
        } else {
            for (const QString& row : example.expect) {
                lines.append(QStringLiteral("→ ") + row);
            }
        }
        html += QStringLiteral("<pre>%1</pre>").arg(esc(lines.join(QLatin1Char('\n'))));
    }
    html += QStringLiteral("<p class=\"note\">列表区的值就是上面示例里的 "
                           "<code>list1</code> = 报告A、报告B、报告C，"
                           "<code>list2</code> = 1、2、3。</p>");
    return html;
}

}  // namespace

QString cheatsheet_html() {
    const QString version = QString::fromLatin1(batchsmith::core::version_string());

    QString html;
    html += QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    html += QStringLiteral("<title>BatchSmith DSL 速查表</title>");
    html += QStringLiteral("<style>") + style_sheet() + QStringLiteral("</style>");
    html += QStringLiteral("</head><body>");

    html += QStringLiteral("<h1>BatchSmith DSL 速查表</h1>");
    html += QStringLiteral("<p class=\"ver\">版本 %1</p>").arg(esc(version));

    // 目录：界面左侧/顶部用它跳转，也方便在浏览器里看
    html += QStringLiteral("<div class=\"toc\"><b>目录</b><ul>");
    for (const CheatSection& section : cheatsheet_sections()) {
        html += QStringLiteral("<li><a href=\"#%1\">%2</a></li>")
                        .arg(esc(section.anchor), esc(section.title));
    }
    html += QStringLiteral("</ul></div>");

    // 正文按**锚点**匹配，不靠数组下标对齐章节顺序：中间插入一章时
    // 下标会静默错位成"标题配错正文"，而锚点写错只会让那一节空着，测试能抓住。
    struct SectionBody {
        const char* anchor;
        QString (*render)();
    };

    static const SectionBody kBodies[] = {
            {"sec-overview", &render_overview},
            {"sec-names", &render_names},
            {"sec-lists", &render_lists},
            {"sec-rows", &render_rows},
            {"sec-helpers", &render_helpers},
            {"sec-lua", &render_lua},
            {"sec-pitfalls", &render_pitfalls},
            {"sec-examples", &render_examples},
            {"sec-preset", &render_preset},
    };

    for (const CheatSection& section : cheatsheet_sections()) {
        html += QStringLiteral("<h2 id=\"%1\">%2</h2>")
                        .arg(esc(section.anchor), esc(section.title));

        QString body;
        for (const SectionBody& candidate : kBodies) {
            if (section.anchor == QLatin1String(candidate.anchor)) {
                body = candidate.render();
                break;
            }
        }
        if (body.isEmpty()) {
            // 漏了正文时在帮助里直接写出来 —— 比静默给一节空标题好得多
            body = QStringLiteral("<p class=\"note\">（这一节还没有正文）</p>");
        }
        html += body;
    }

    html += QStringLiteral("</body></html>");
    return html;
}

QString cheatsheet_hhc() {
    // CHM 的目录文件（HTML Help 的 site map）。两级：
    //   顶层 = 各章节；二级 = "工具函数"与"示例"两章里的分组。
    // 全部由数据生成 —— 与界面帮助窗口的目录、`bs cheatsheet` 的章节同源。
    QString hhc;
    hhc += QStringLiteral("<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n");
    hhc += QStringLiteral("<HTML><HEAD>\n");
    // .hhc 是给 HTML Help 读的，显式声明编码，免得中文目录变乱码
    hhc += QStringLiteral("<meta http-equiv=\"Content-Type\" "
                          "content=\"text/html; charset=utf-8\">\n");
    hhc += QStringLiteral("<meta name=\"GENERATOR\" content=\"BatchSmith %1\">\n")
                   .arg(esc(QString::fromLatin1(batchsmith::core::version_string())));
    hhc += QStringLiteral("</HEAD><BODY>\n");
    hhc += QStringLiteral("<OBJECT type=\"text/site properties\">\n");
    hhc += QStringLiteral("    <param name=\"Window Styles\" value=\"0x800025\">\n");
    hhc += QStringLiteral("</OBJECT>\n");
    hhc += QStringLiteral("<UL>\n");

    const QList<CheatSection> sections_list = cheatsheet_sections();
    const QList<GroupAnchor> helper_anchors =
            group_anchors(unique_helper_groups(), QStringLiteral("grp-"));
    const QList<GroupAnchor> example_anchors =
            group_anchors(unique_example_groups(), QStringLiteral("ex-"));

    for (const CheatSection& section : sections_list) {
        hhc += QStringLiteral("  <LI><OBJECT type=\"text/sitemap\">\n");
        hhc += QStringLiteral("      <param name=\"Name\" value=\"%1\">\n").arg(esc(section.title));
        hhc += QStringLiteral("      <param name=\"Local\" value=\"index.html#%1\">\n")
                       .arg(esc(section.anchor));
        hhc += QStringLiteral("      </OBJECT>\n");

        // 按**锚点名**判断（而不是下标）：章节顺序调整时这里不会静默错位
        QList<GroupAnchor> children;
        if (section.anchor == QLatin1String("sec-helpers")) {
            children = helper_anchors;
        } else if (section.anchor == QLatin1String("sec-examples")) {
            children = example_anchors;
        }
        if (children.isEmpty()) {
            continue;
        }

        hhc += QStringLiteral("    <UL>\n");
        for (const GroupAnchor& child : children) {
            hhc += QStringLiteral("      <LI><OBJECT type=\"text/sitemap\">\n");
            hhc += QStringLiteral("          <param name=\"Name\" value=\"%1\">\n")
                           .arg(esc(child.title));
            hhc += QStringLiteral("          <param name=\"Local\" value=\"index.html#%1\">\n")
                           .arg(esc(child.id));
            hhc += QStringLiteral("          </OBJECT>\n");
        }
        hhc += QStringLiteral("    </UL>\n");
    }

    hhc += QStringLiteral("</UL>\n</BODY></HTML>\n");
    return hhc;
}

}  // namespace batchsmith::core::dsl
