#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include "MainWindow.h"
#include "batchsmith/core/preset/preset.hpp"
#include "batchsmith/core/version.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BatchSmith"));
    QApplication::setApplicationVersion(QString::fromLatin1(batchsmith::core::version_string()));
    QApplication::setOrganizationName(QStringLiteral("BatchSmith"));
    // 供 Linux 桌面环境把窗口与 .desktop 文件对应起来（Wayland 下必需）
    QApplication::setDesktopFileName(QStringLiteral("BatchSmith"));

    // 命令行只做一件事：启动时带一个预设。GUI 的其它功能都在界面里，
    // 不给它加命令行开关 —— 需要脚本化的是 `bs`（CLI），不是窗口程序。
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BatchSmith —— 把列表与表达式编译成批量操作"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOption(QCommandLineOption(QStringLiteral("preset"),
                                        QStringLiteral("启动时载入的预设文件（.toml）"),
                                        QStringLiteral("文件")));
    parser.addPositionalArgument(QStringLiteral("预设"),
                                 QStringLiteral("可选：启动时要载入的 .toml 预设文件"));

    parser.process(app);

    MainWindow window;

    // 位置参数与 --preset 都支持：前者方便双击/终端里直接拖文件，后者方便写快捷方式
    QString preset_path = parser.value(QStringLiteral("preset"));
    if (preset_path.isEmpty()) {
        const QStringList positional = parser.positionalArguments();
        if (!positional.isEmpty()) {
            preset_path = positional.first();
        }
    }

    if (!preset_path.isEmpty()) {
        QString error;
        if (!window.openPreset(preset_path, &error)) {
            // 不能静默启动一个空窗口：用户会觉得"打开失败但不知道为什么"。
            // 窗口照常显示（他还能手动去打开别的预设），但原因要说清楚。
            QMessageBox::warning(
                    &window,
                    QStringLiteral("载入预设失败"),
                    QStringLiteral("%1\n\n%2")
                            .arg(preset_path,
                                 error.isEmpty() ? QStringLiteral("（无更多信息）") : error));
        }
    }

    window.show();
    return QApplication::exec();
}
