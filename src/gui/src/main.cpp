#include <QApplication>

#include "MainWindow.h"
#include "batchsmith/core/version.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BatchSmith"));
    QApplication::setApplicationVersion(QString::fromLatin1(batchsmith::core::version_string()));
    QApplication::setOrganizationName(QStringLiteral("BatchSmith"));
    // 供 Linux 桌面环境把窗口与 .desktop 文件对应起来（Wayland 下必需）
    QApplication::setDesktopFileName(QStringLiteral("BatchSmith"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
