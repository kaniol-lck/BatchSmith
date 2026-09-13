#include "batchsmith/core/version.hpp"

namespace batchsmith::core {
namespace {

// 都是编译期字面量拼接，放在匿名命名空间的常量数组里，
// 避免每次调用做格式化。
constexpr char kVersionString[] = BATCHSMITH_VERSION_STRING;

constexpr char kBanner[] = "BatchSmith " BATCHSMITH_VERSION_STRING " (" BATCHSMITH_BUILD_TYPE
                           ", " BATCHSMITH_COMPILER_ID ", Qt " BATCHSMITH_QT_VERSION ")";

}  // namespace

const char* version_string() noexcept {
    return kVersionString;
}

const char* version_banner() noexcept {
    return kBanner;
}

}  // namespace batchsmith::core
