#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

#include "batchsmith/core/version.hpp"

TEST_CASE("版本号来自 CMake 的 project() 版本") {
    // 防止有人只改了 CMakeLists.txt 的版本号而这里悄悄失配
    CHECK(QString::fromLatin1(batchsmith::core::version_string()) ==
          QStringLiteral(BATCHSMITH_VERSION_STRING));

    CHECK(QStringLiteral(BATCHSMITH_VERSION_STRING).split(QLatin1Char('.')).size() == 3);
}

TEST_CASE("版本横幅包含构建信息") {
    const QString banner = QString::fromLatin1(batchsmith::core::version_banner());

    CHECK(banner.startsWith(QStringLiteral("BatchSmith ")));
    CHECK(banner.contains(QStringLiteral(BATCHSMITH_VERSION_STRING)));
    CHECK(banner.contains(QStringLiteral(BATCHSMITH_BUILD_TYPE)));
    CHECK(banner.contains(QStringLiteral("Qt ")));
}
