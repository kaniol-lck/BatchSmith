# third_party —— 依赖（git submodule）

本目录下的四个依赖通过 **git submodule** 管理，各自锁在一个 release tag 上。
第三方代码不进本仓库的对象库，所以 `git log` / `git diff` / 语言统计都是干净的。

## 获取

```bash
git clone --recursive <仓库地址>

# 已经有本地副本、或忘了加 --recursive 时：
git submodule update --init --recursive
```

> `.gitmodules` 里为每个依赖设了 `shallow = true`，首次拉取走浅克隆，不用下完整历史。
>
> **忘了加 `--recursive` 会怎样**：`third_party/*/` 是空目录，CMake 在配置阶段就直接报错
> 并提示上面这条命令，不会让你在一堆「找不到头文件」的编译错误里猜。
> 这是刻意设计的 —— 未初始化的 submodule 是这套方案最常见的翻车点。

## 清单

| 依赖 | 锁定版本 | 仓库 | 许可 | 用途 |
|---|---|---|---|---|
| [Lua](https://github.com/lua/lua) | **v5.4.7** | `lua/lua` | MIT（见 `lua.h` 末尾的版权段） | 表达式运行时 |
| [sol2](https://github.com/ThePhD/sol2) | **v3.3.0** | `ThePhD/sol2` | MIT（`sol2/LICENSE.txt`） | Lua 的 C++ 绑定 |
| [toml++](https://github.com/marzer/tomlplusplus) | **v3.4.0** | `marzer/tomlplusplus` | MIT（`tomlplusplus/LICENSE`） | 预设文件解析 |
| [doctest](https://github.com/doctest/doctest) | **v2.4.11** | `doctest/doctest` | MIT（`doctest/LICENSE.txt`） | 单元测试（**不进发布产物**） |

四个都是 MIT，与项目的 GPL-3.0 相容。

## 升级某个依赖

```bash
cd third_party/<name>
git fetch --depth 1 origin tag <新版本>
git checkout <新版本>
cd ../..
git add third_party/<name>
```

然后更新上表的版本号，并跑一遍 `ctest` 确认没破坏东西。

> **注意**：submodule 在父仓库里记录的是 **commit SHA 而不是 tag 名**。
> 所以升级是一次需要提交的改动，别人 `git submodule update` 拿到的会是完全相同的提交 ——
> 这比记一个 tag 名更严格（tag 可以被上游移动，SHA 不会）。

## include 路径约定（**四个依赖的布局并不统一**）

各依赖的内部结构与上游保持一致，`#include` 写法与上游文档相同。但布局要留意：

| 依赖 | 头文件实际位置 | CMake 里的 include 目录 |
|---|---|---|
| Lua | `lua/lua.h` —— 该仓库根目录**就是** src 目录 | `third_party/lua` |
| sol2 | `sol2/include/sol/sol.hpp` | `third_party/sol2/include` |
| toml++ | `tomlplusplus/include/toml++/toml.hpp` | `third_party/tomlplusplus/include` |
| doctest | `doctest/doctest/doctest.h` | `third_party/doctest` |

**doctest 那一行最容易写错**：它的头文件在仓库根的 `doctest/` 子目录下，
所以 include 目录是**仓库根**而不是 `include/`。

另外 Lua 的构建需要排除 `lua.c` / `luac.c`（官方解释器与编译器的 `main()`），
见 `CMakeLists.txt` 里的 `list(FILTER ... EXCLUDE REGEX ...)`。

## 为什么是 submodule

先前用过 vendored 快照（把文件直接提交进仓库），能跑通，但会把第三方代码写进本仓库的
提交历史。改用 submodule 后本仓库不含任何第三方代码：

| 方案 | 本仓库是否含第三方代码 | 本仓库自身体积 | clone 完能否直接构建 |
|---|---|---|---|
| **submodule（当前）** | ❌ 否，只有四行 gitlink | **305 KB** | ❌ 需 `--recursive`（有一处即可） |
| vendored 快照 | ✅ 是，进提交历史 | 3.4 MB | ✅ |

实测 submodule 的首次获取成本（浅克隆，工作树合计约 46 MB，其中相当一部分是我们用不到的
文档、示例与测试）：

| 依赖 | 工作树 |
|---|---|
| sol2 | ~31 MB（含 8.4 MB 的 `.git`） |
| toml++ | ~8 MB |
| doctest | ~9 MB |
| lua | ~3 MB |

**选择理由**：不让第三方代码污染自己的仓库这一条更重要。代价是首次 clone 需要网络，
且必须记得 `--recursive` —— 后者已用 CMake 的显式报错兜住。
