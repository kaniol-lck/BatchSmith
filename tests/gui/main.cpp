#define DOCTEST_CONFIG_IMPLEMENT

#include <doctest/doctest.h>

#include <QApplication>

#include "ui_shot.hpp"

/// GUI 测试的入口：与 core 的测试不同，这里必须先把 QApplication 建起来
/// （构造任何 QWidget 都需要它）。测试用 `QT_QPA_PLATFORM=offscreen` 运行
/// （见 tests/CMakeLists.txt 里 ctest 的环境设置），所以 CI 上不需要显示服务，
/// 也不会真的弹窗口出来。
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 只服务于「渲染一张版面截图给人看」，与测试断言无关。
    // 用环境变量而不是命令行参数，是为了不干扰 doctest 自己的参数解析。
    const QByteArray shotPath = qgetenv("BATCHSMITH_UI_SHOT");
    if (!shotPath.isEmpty()) {
        return capture_ui_shot(QString::fromLocal8Bit(shotPath));
    }

    return doctest::Context(argc, argv).run();
}
