#pragma once

#include <QStringList>

namespace batchsmith::core::sandbox {
class Sandbox;
}

namespace batchsmith::core::helper {

/// 实际注册进沙箱的工具函数名（按注册顺序）。
///
/// 存在的理由是**可校验**：帮助文档里写了哪些函数，与这里实际注册了哪些，
/// 必须完全一致（`tests/core/test_cheatsheet.cpp` 做双向比对）。
/// 少了这个出口，"文档漏写一个新的 helper" 就只能靠人肉 review 发现。
[[nodiscard]] QStringList registered_names();

/// 把 DSL 的 helper 集合注册进沙箱 env（技术方案 §3.5 的六类，外加 0.4 起的「文件名」类）。
///
/// 与原生写法并存且结果一致：`index(l,i)` ≡ `l[i]`、`count(l)` ≡ `#l`
/// （构想书明确要求这一点，便于不熟 Lua 的用户迁移）。
void register_helpers(sandbox::Sandbox& sandbox);

/// 覆写三处「先分配后检查」的库函数（ADR-6 逃逸面 1），并把 `math.random` 换成
/// 沙箱自己的可复现随机源（dry-run 与真实执行必须给出相同结果）。
///
/// 同时把 `math.randomseed` 替换为 `seed(n)` 的等价物。
void patch_string_and_table_functions(sandbox::Sandbox& sandbox);

}  // namespace batchsmith::core::helper
