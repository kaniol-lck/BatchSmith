/// 「创建快捷方式」的测试。
///
/// 这一组盯的是**建出来的东西真的能用**：快捷方式指向的程序必须是本程序的 exe，
/// 参数里必须带着预设的**绝对**路径。
///
/// 为什么值得单独测：建一个指向空气的快捷方式比不建更糟 —— 用户双击之后只会得到
/// "打开了一个空白窗口"，而原因（程序路径不对？参数没带上？路径是相对的？）
/// 在界面上完全看不到。所以要能读回来核对。
///
/// 用临时目录当"桌面"：测试不该往开发机真实桌面上放东西。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include "preset/PresetShortcut.h"

#include "doctest/doctest.h"

namespace {

[[nodiscard]] QString S(const char16_t* text) {
    return QString::fromUtf16(text);
}

/// 写一份最小的合法预设
[[nodiscard]] bool write_preset(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray text = QStringLiteral("[preset]\n"
                                           "name = '测试'\n"
                                           "version = 1\n\n"
                                           "[output]\n"
                                           "template = '$list1[i]$'\n")
                                    .toUtf8();
    const bool ok = file.write(text) == text.size();
    file.close();
    return ok;
}

}  // namespace

TEST_CASE("快捷方式：建出来的能读回来，程序与预设都对得上") {
    QTemporaryDir dir;
    QTemporaryDir desktop;
    REQUIRE(dir.isValid());
    REQUIRE(desktop.isValid());

    const QString preset = QDir(dir.path()).filePath(S(u"番剧重命名.toml"));
    REQUIRE(write_preset(preset));

    QString error;
    const QString shortcut =
            create_preset_shortcut(ShortcutRequest{preset, desktop.path()}, &error);
    REQUIRE_MESSAGE(!shortcut.isEmpty(), error.toStdString());

    // 扩展名按平台来
    CHECK(QFileInfo(shortcut).suffix() == shortcut_suffix().mid(1));
    CHECK(QFileInfo(shortcut).completeBaseName() == S(u"番剧重命名"));

    ShortcutTarget target;
    REQUIRE_MESSAGE(read_shortcut(shortcut, &target, &error), error.toStdString());

    // 指向本程序的 exe —— 这是"双击能打开"的前提
    CHECK(target.program == QCoreApplication::applicationFilePath());
    // 参数里带着预设的**绝对**路径
    CHECK(target.passes_preset());
    CHECK(target.preset_path() == QFileInfo(preset).absoluteFilePath());
}

TEST_CASE("快捷方式：预设不存在就不建，并说清是哪一个") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString error;
    const QString missing = QDir(dir.path()).filePath(S(u"并不存在.toml"));
    CHECK(create_preset_shortcut(ShortcutRequest{missing, dir.path()}, &error).isEmpty());
    CHECK(error.contains(S(u"并不存在")));

    // 失败时不该留下半个文件
    CHECK(QDir(dir.path()).entryList(QDir::Files).isEmpty());
}

TEST_CASE("快捷方式：目标目录不存在就建出来，而不是失败") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString preset = QDir(dir.path()).filePath(S(u"a.toml"));
    REQUIRE(write_preset(preset));

    // 用户可能把快捷方式目录指到一个还没建的文件夹（比如刚同步过来的桌面）
    const QString nested = QDir(dir.path()).filePath(S(u"还没建的目录/里面"));
    QString error;
    const QString shortcut = create_preset_shortcut(ShortcutRequest{preset, nested}, &error);
    REQUIRE_MESSAGE(!shortcut.isEmpty(), error.toStdString());
    CHECK(QFileInfo::exists(shortcut));
}

TEST_CASE("快捷方式：路径里有空格也原样带过去") {
    QTemporaryDir base;
    REQUIRE(base.isValid());

    // 空格是最常见、也最容易把命令行拼错的东西（少了引号就会被拆成两个参数）
    const QString folder = QDir(base.path()).filePath(S(u"带 空格 的目录"));
    REQUIRE(QDir().mkpath(folder));

    const QString preset = QDir(folder).filePath(S(u"番 剧 重命名.toml"));
    REQUIRE(write_preset(preset));

    QString error;
    const QString shortcut = create_preset_shortcut(ShortcutRequest{preset, folder}, &error);
    REQUIRE_MESSAGE(!shortcut.isEmpty(), error.toStdString());
    CHECK(QFileInfo(shortcut).completeBaseName() == S(u"番 剧 重命名"));

    ShortcutTarget target;
    REQUIRE_MESSAGE(read_shortcut(shortcut, &target, &error), error.toStdString());
    CHECK(target.program == QCoreApplication::applicationFilePath());
    CHECK(target.preset_path() == QFileInfo(preset).absoluteFilePath());
}

TEST_CASE("快捷方式：重名加序号，不覆盖已经建好的那个") {
    QTemporaryDir dir;
    QTemporaryDir desktop;
    REQUIRE(dir.isValid());
    REQUIRE(desktop.isValid());

    const QString preset = QDir(dir.path()).filePath(S(u"同一个.toml"));
    REQUIRE(write_preset(preset));

    QString error;
    const QString first = create_preset_shortcut(ShortcutRequest{preset, desktop.path()}, &error);
    REQUIRE_MESSAGE(!first.isEmpty(), error.toStdString());

    const QString second = create_preset_shortcut(ShortcutRequest{preset, desktop.path()}, &error);
    REQUIRE_MESSAGE(!second.isEmpty(), error.toStdString());
    CHECK(second != first);
    CHECK(QFileInfo(second).completeBaseName().startsWith(S(u"同一个")));

    // 两个都在：去重不能靠覆盖
    CHECK(QFileInfo::exists(first));
    CHECK(QFileInfo::exists(second));
}

TEST_CASE("快捷方式：自定义图标能带过去") {
    QTemporaryDir dir;
    QTemporaryDir desktop;
    REQUIRE(dir.isValid());
    REQUIRE(desktop.isValid());

    const QString preset = QDir(dir.path()).filePath(S(u"带图标.toml"));
    REQUIRE(write_preset(preset));

    // 图标文件本身不校验内容（快捷方式只存路径，渲染是 shell 的事），但路径要真的存在
    const QString icon = QDir(dir.path()).filePath(S(u"番剧.png"));
    {
        QFile file(icon);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("placeholder");
    }

    QString error;
    const QString shortcut = create_preset_shortcut(
            ShortcutRequest{preset, desktop.path(), QFileInfo(icon).absoluteFilePath()}, &error);
    REQUIRE_MESSAGE(!shortcut.isEmpty(), error.toStdString());

    ShortcutTarget target;
    REQUIRE_MESSAGE(read_shortcut(shortcut, &target, &error), error.toStdString());
#if defined(Q_OS_MACOS)
    // `.command` 没有图标位置：这个参数被忽略（文档里写明了，不是静默失败）
    CHECK(target.icon.isEmpty());
#else
    CHECK(target.icon == QFileInfo(icon).absoluteFilePath());
#endif

    // 不设图标时不写图标 —— 免得指向一个空路径
    const QString plain = create_preset_shortcut(ShortcutRequest{preset, desktop.path()}, &error);
    REQUIRE_MESSAGE(!plain.isEmpty(), error.toStdString());
    ShortcutTarget plain_target;
    REQUIRE_MESSAGE(read_shortcut(plain, &plain_target, &error), error.toStdString());
    CHECK(plain_target.icon.isEmpty());
}

TEST_CASE("快捷方式：读不了的文件要报错，而不是给一个空结果") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ShortcutTarget target;
    QString error;
    // 文件不存在
    CHECK_FALSE(read_shortcut(QDir(dir.path()).filePath(S(u"没有这个.lnk")), &target, &error));
    CHECK_FALSE(error.isEmpty());

    // 存在但不是快捷方式（比如用户把一个文本文件改了名）——
    // 这时候**不能**返回"读到了，但程序是空的"，那会让调用方以为一切正常
    const QString junk = QDir(dir.path()).filePath(S(u"假的.lnk"));
    {
        QFile file(junk);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("this is not a shortcut");
    }
    error.clear();
    CHECK_FALSE(read_shortcut(junk, &target, &error));
    CHECK_FALSE(error.isEmpty());
    CHECK(target.program.isEmpty());
}

TEST_CASE("快捷方式：参数解析认得 --preset 与 --preset= 两种写法") {
    ShortcutTarget target;

    target.arguments = QStringLiteral("--preset \"C:/a b/x.toml\"");
    CHECK(target.passes_preset());
    CHECK(target.preset_path() == S(u"C:/a b/x.toml"));

    target.arguments = QStringLiteral("--preset C:/no-space/x.toml");
    CHECK(target.preset_path() == S(u"C:/no-space/x.toml"));

    // 用户手改过的话多半是这个写法
    target.arguments = QStringLiteral("--preset=C:/y.toml");
    CHECK(target.preset_path() == S(u"C:/y.toml"));

    // 没有 --preset：不算"带着预设启动"（这时候双击只是开个空窗口，
    // 调用方据此可以给出提示）
    target.arguments = QStringLiteral("--other \"C:/y.toml\"");
    CHECK_FALSE(target.passes_preset());
    CHECK(target.preset_path().isEmpty());

    target.arguments.clear();
    CHECK_FALSE(target.passes_preset());
}
