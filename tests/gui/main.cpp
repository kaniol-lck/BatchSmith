#define DOCTEST_CONFIG_IMPLEMENT

#include <doctest/doctest.h>

#include <cstdio>

#include <QApplication>
#include <QStandardPaths>
#include <QString>

#include "ui_shot.hpp"

/// GUI 测试的入口：与 core 的测试不同，这里必须先把 QApplication 建起来
/// （构造任何 QWidget 都需要它）。测试用 `QT_QPA_PLATFORM=offscreen` 运行
/// （见 tests/CMakeLists.txt 里 ctest 的环境设置），所以 CI 上不需要显示服务，
/// 也不会真的弹窗口出来。
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 关掉输出缓冲：测试若以崩溃收场，缓冲区里没落盘的内容会连进程一起消失，
    // 而"崩溃前最后跑到的用例名"往往是唯一的定位线索（见 tests/main.cpp 的说明）。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // ---- 测试隔离：别碰开发机上真实的设置与预设目录 ----
    //
    // 预设相关的用例会往"用户预设目录"里写文件、也会读写"最近打开"。
    // 用独立的组织/应用名 + Qt 的测试模式，让这些落到测试专用的位置；
    // 否则跑一次测试就会污染开发者的实际配置（"最近打开"里冒出一堆临时文件）。
    QApplication::setOrganizationName(QStringLiteral("BatchSmithTest"));
    QApplication::setApplicationName(QStringLiteral("BatchSmithTest"));
    QStandardPaths::setTestModeEnabled(true);

    // 只服务于「渲染一张版面截图给人看」，与测试断言无关。
    // 用环境变量而不是命令行参数，是为了不干扰 doctest 自己的参数解析。
    const QByteArray shotPath = qgetenv("BATCHSMITH_UI_SHOT");
    if (!shotPath.isEmpty()) {
        return capture_ui_shot(QString::fromLocal8Bit(shotPath));
    }

    // 帮助对话框另存一张：内容较长，单独渲染才看得清排版
    const QByteArray helpShotPath = qgetenv("BATCHSMITH_HELP_SHOT");
    if (!helpShotPath.isEmpty()) {
        return capture_help_shot(QString::fromLocal8Bit(helpShotPath));
    }

    return doctest::Context(argc, argv).run();
}
