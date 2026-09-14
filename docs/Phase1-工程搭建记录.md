# Phase 1 施工记录 —— 工程搭建

> 本文是 **Phase 1（POC / 技术验证）** 的施工细节：具体命令、实测数据、踩过的坑。
> 阶段目标与验收标准见 [`项目阶段规划.md`](项目阶段规划.md)。
>
> 命名说明：**P 已经用于项目阶段（Phase），所以本文内部的施工步骤改用 S（Step）编号**，
> 避免两套 P 互相指错。

## 状态总览

| 步骤 | 内容 | 状态 |
|---|---|---|
| S1 | 工程骨架文件（.gitignore / .editorconfig / .clang-format / CMakePresets / README） | ✅ 已完成 |
| S2 | 第三方依赖：改用 **git submodule** 管理 | ✅ 已完成 |
| S3 | CMake 工程结构与三个 target（core / bs / batchsmith） | ✅ 已完成 |
| S4 | 测试链路与构建验证 | ✅ 已完成 |
| S5 | 打包脚本（三平台）+ 本机实测 | ✅ 已完成（Windows 路径已端到端验证） |
| S6 | CI：三平台构建 + 打包 + 打 tag 自动发版 | ⬜ 已写好，**待首次推送验证** |

> 原先列为 P7 的「静态 Qt 构建管线」已移到 [`项目阶段规划.md`](项目阶段规划.md) 的 **Phase 8**
> —— 它属于分发与打磨，不是 Phase 1 的工程搭建内容。

---

## S1 工程骨架文件

**产出**

| 文件 | 作用 |
|---|---|
| `.gitignore` | 忽略构建产物、IDE、OS 垃圾；`CMakeUserPresets.json` 与 `.workbuddy/memory/` 不进库 |
| `.gitattributes` | 统一 LF 换行；`third_party/**` 标记为 vendored |
| `.editorconfig` | 4 空格缩进、UTF-8、LF；补齐编辑器差异 |
| `.clang-format` | C++20 格式规范，100 列 |
| `README.md` | 项目定位、选型、构建与运行 |
| `CMakePresets.json` | 可移植构建预设（committed） |
| `CMakeUserPresets.json` | 本机工具链绝对路径（gitignored） |
| `LICENSE` | 已有（GPL-3.0） |

**为什么单独分一步**：这些文件之间没有依赖，但少任何一个都会在第一个人
clone 时踩坑。先一次性铺完，后续步骤不必回头补。

**验证**

```bash
git status --short          # 不应出现构建产物
```

---

## S2 第三方依赖：改用 git submodule

**最终做法**：四个依赖用 **git submodule** 管理，各锁在一个 release tag 上。

**演变过程（值得记录，因为结论反转过）**

1. 起初做的是 **vendored 快照** —— 把文件直接提交进 `third_party/`。它跑通了，
   优点是 `git clone` 完就能直接构建，不需要任何网络。
2. 但用户明确提出**不想让第三方代码污染自己的仓库**，于是改为 submodule。
   本仓库自身体积因此从 3.4 MB 降到 **约 300 KB**（只留四行 gitlink）。

**取舍对比（都是实测数字）**

| 方案 | 本仓库是否含第三方代码 | 本仓库自身体积 | 首次获取 | clone 完能否直接构建 |
|---|---|---|---|---|
| **submodule（采用）** | ❌ 否 | **~300 KB** | 浅克隆，工作树合计约 46 MB | ❌ 需 `--recursive` |
| vendored 快照 | ✅ 是 | 3.4 MB | 无 | ✅ |
| CMake FetchContent | ❌ 否 | ~300 KB | 约 46 MB，且在 configure 阶段 | ❌ |

**注意**：先前一度以为"子模块/FetchContent 需要联网而本机网络不可靠"是选 vendored 的理由 ——
**这个判断是错的**，实测 `git ls-remote` / `git clone` 到四个上游仓库都正常。
真实差异只是**体积**（46 MB vs 3.4 MB，而本仓库自身体积是 300 KB vs 3.4 MB）
和**是否需要 `--recursive`**。

**产出**

```
third_party/
├── CMakeLists.txt          # 定义 lua54 / sol2 / tomlplusplus / doctest 四个 target
├── README.md               # 版本、来源、许可、升级方式、踩过的坑
├── lua/                    # submodule, v5.4.7   @ 1ab3208
├── sol2/                   # submodule, v3.3.0   @ eba8662
├── tomlplusplus/           # submodule, v3.4.0   @ 3017243
└── doctest/                # submodule, v2.4.11  @ ae7a135
```

**要点**

- `.gitmodules` 里每个依赖设了 `shallow = true`，首次拉取走浅克隆。
- **`lua.c` / `luac.c` 必须从库里排除** —— 它们含 `main()`。
  CMake 里用 `list(FILTER ... EXCLUDE REGEX "/(lua|luac)\\.c$")` 处理。
- 第三方 target **不链接** `batchsmith_warnings`，因此本项目的严格警告策略不会打进
  上游代码，不必为迁就上游写法而关掉自己的检查。
- Windows 不需要显式定义 `LUA_USE_WINDOWS` —— `luaconf.h` 在 `_WIN32` 下自动打开。
  Linux / macOS 需显式定义 `LUA_USE_LINUX` / `LUA_USE_MACOSX`。
- **CMake 在配置阶段检查各依赖的关键头文件是否存在**，缺失时直接给出
  `git submodule update --init --recursive` 的指引 —— 未初始化 submodule 是这套方案
  最常见的翻车点，不该让人在一堆"找不到头文件"里猜。

### ⚠️ 四个依赖的 include 布局并不统一

| 依赖 | 头文件实际位置 | CMake 里的 include 目录 |
|---|---|---|
| Lua | `lua/lua.h` —— 该仓库根目录**就是** src 目录 | `third_party/lua` |
| sol2 | `sol2/include/sol/sol.hpp` | `third_party/sol2/include` |
| toml++ | `tomlplusplus/include/toml++/toml.hpp` | `third_party/tomlplusplus/include` |
| doctest | `doctest/doctest/doctest.h` | `third_party/doctest` |

**doctest 那一行最容易写错**：它的头文件在仓库根的 `doctest/` 子目录下，
所以 include 目录是**仓库根**而不是 `include/`。（vendored 时我手工把结构摆成了
`include/doctest/doctest.h`，改成 submodule 后必须改回上游布局。）

### 迁移到 submodule 时踩的坑

1. **`git submodule add --depth 1 -b <tag>` 不成立。** 报
   `'origin/v5.4.7' is not a commit and a branch 'v5.4.7' cannot be created from it`
   —— 浅克隆只取了默认分支的尖端，取不到旧 tag。
   正确做法是**先浅克隆到 tag，再手工登记**：

   ```bash
   git clone --depth 1 --branch <tag> <url> third_party/<name>
   git config -f .gitmodules submodule.third_party/<name>.path  third_party/<name>
   git config -f .gitmodules submodule.third_party/<name>.url   <url>
   git config -f .gitmodules submodule.third_party/<name>.shallow true
   git add .gitmodules third_party/<name>
   ```

2. **`git submodule absorbgitdirs` 需要先 `git submodule init`。** 手工登记的 gitlink
   在进索引之前 `git submodule status` 是空的，`absorbgitdirs` 也会跳过它们，
   结果出现"`third_party/lua/.git` 是目录、同时 `.git/modules/third_party/lua` 也存在"
   的重复状态。解法：先 `git add` 登记 gitlink，再 `git submodule init`，最后
   `absorbgitdirs`。若已出现重复，删掉 `.git/modules/` 下的副本即可回到
   合法的"未吸收"状态（`.git` 为目录本身也是有效的 submodule 形态）。

### 获取依赖时踩的坑（升级时会再遇到）

- GitHub Release 资产地址 `github.com/.../releases/download/...` 会 302 到
  `objects.githubusercontent.com`，本机经注入代理访问该域名返回 **502**。
  改用 `codeload.github.com`（仓库 tarball）或 `raw.githubusercontent.com`（单文件）。
  备用镜像：`https://cdn.jsdelivr.net/gh/<owner>/<repo>@<tag>/<path>`（实测可用）。
- GitHub 通路是**间歇性**的：同一命令可能前一分钟成功、后一分钟 502。
  脚本里要做重试，别把一次失败当成"网络不通"。
- sol2 **v3.3.0 的仓库里没有预生成的 single 头**（`single/include/sol/sol.hpp` 返回 404），
  只有模块化 `include/sol/` 树。这正是现在以 submodule 形式 vendor 整个 `include/` 的原因。

---

## S3 CMake 工程结构与三个 target

**产出**

```
CMakeLists.txt                 顶层：C++20、选项、Qt 查找、警告策略、LTO、配置摘要
src/core/CMakeLists.txt        batchsmith_core（STATIC）
src/cli/CMakeLists.txt         bs
src/gui/CMakeLists.txt         batchsmith（AUTOMOC/AUTOUIC/AUTORCC 已打开）
tests/CMakeLists.txt           batchsmith_tests
```

**关键约定**

1. **`core` 只依赖 `Qt6::Core`**（技术方案 ADR-9）。不引用 QtGui/QtWidgets，
   于是能在无图形环境跑测试，也为将来换 GUI 框架留余地。
   `lua54` / `sol2` / `tomlplusplus` 是它的 **PRIVATE** 依赖 —— 公共头文件不包含它们。
2. **版本头由 CMake 生成**（`version.hpp.in` → `version.hpp`），版本号只在
   顶层 `project(... VERSION ...)` 维护一处。
3. **禁止源码内构建**：顶层显式 `FATAL_ERROR` 拦截，避免把 `CMakeCache.txt`
   这类垃圾丢进版本库。
4. **警告策略只作用于本项目代码**（`batchsmith_warnings` INTERFACE target），
   由各目标显式链接。
5. **本地消息一律走 `QTextStream`，不用 `qDebug`** —— Release 构建若带
   `QT_NO_DEBUG_OUTPUT`，`qDebug` 会被编译期屏蔽，而 CLI 的输出是对外契约。

**已交付的 core 功能**（Phase 1 的验收项之一，见项目阶段规划）

- `batchsmith::core::version_string()` / `version_banner()`
- `batchsmith::core::natural_compare(a, b, cs)` / `natural_less(...)`

自然排序是**正确性**要求而非体验优化：列表源绑定文件夹后，默认排序若按字典序，
`file10` 会排在 `file2` 之前，而"文件行绑定绝对路径"的安全模型依赖用户能凭排序
直观核对结果（技术方案 §4.4 / §4.6）。

---

## S4 测试链路与构建验证

**做法**：doctest 单可执行文件 + 一个 ctest 条目。

- 用 doctest 而非 Qt Test：`core` 是纯逻辑库，不该被 Qt 的测试框架绑定。
  GUI 层的测试到 Phase 6 再单独引入 QTest。
- `batchsmith_tests` 只链接 core，**不碰 QtWidgets** → CI 上跑测试不必安装 X11 依赖。

**验证命令**

```bash
cmake --preset windows-mingw
cmake --build --preset windows-mingw
ctest --preset windows-mingw --output-on-failure
```

### ⚠️ 本机最大的一个坑：`PATH` 里必须有 MinGW 的 `bin`

**症状**：CMake 报

```
Check for working C compiler: .../gcc.exe - broken
The C compiler ... is not able to compile a simple test program.
Run Build Command(s): ninja.exe -v cmTC_xxxx
  ... gcc.exe -o ...testCCompiler.c.obj -c ...testCCompiler.c
  FAILED: ...
```

**关键点是它不打印任何编译器错误信息** —— 只有 `FAILED` 和 `ninja: build stopped`。
后来手工编译 `int main(){}` 也只见 `rc=1`、零输出，极难定位。

**根因**：`cc1.exe`（真正的编译器）在
`C:/Qt/Tools/mingw1120_64/libexec/gcc/x86_64-w64-mingw32/11.2.0/` 下，
而它依赖的运行时 DLL（`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`、
`libmpc-3.dll`、`libisl-*.dll` 等）在 `bin/` 下。Windows 的 DLL 搜索路径**包含
可执行文件自身所在目录，但不包含它的兄弟目录** —— 于是 `gcc.exe` 能跑（DLL 与它同目录），
`cc1.exe` 跑不起来，进程静默死掉，gcc 拿不到任何输出来报告。

**这不是沙箱问题**（关掉沙箱复现同样症状），CMake 预设里把编译器写成绝对路径也救不了。

**解法**：构建/测试/运行产物之前，PATH 里必须有工具链的 `bin`：

```bash
export PATH="/c/Qt/Tools/mingw1120_64/bin:/c/Qt/6.7.2/mingw_64/bin:$PATH"
```

> S5 的三个打包脚本与 CI 就是这样做的：把 PATH 设置一次固化在流程里。

### 实测结果（Windows / MinGW 11.2 / Qt 6.7.2）

| 项目 | 结果 |
|---|---|
| Debug 构建 | ✅ 成功，**0 警告 0 错误** |
| Release 构建（`-Werror` 严格模式） | ✅ 成功，0 警告 |
| `ctest`（Debug / Release 各 1 个测试可执行文件） | ✅ 10 个用例 / 246 个断言全通过 |
| `clang-format 18.1.8 --dry-run --Werror` | ✅ 全部合规 |
| CLI `bs --version` | ✅ 输出 `bs 0.1.0` |
| CLI 无参数退出码 | ✅ `2`（避免被脚本误判为成功） |
| CLI 中文输出 | ✅ 无乱码（Windows 控制台已切 UTF-8） |
| GUI 启动 | ✅ **窗口确实存在**：`MainWindowTitle=[BatchSmith] Responding=True`（修复 `onelua.c` 后重新验证，见下） |
| GUI AUTOMOC 元对象链路 | ✅ `mocs_compilation.cpp` 正常编译，`Q_OBJECT` 可用 |
| Release 产物体积 | `bs.exe` 0.06MB、`batchsmith.exe` 0.10MB |
| GUI 精简部署目录 / 压缩包 | **27.8MB / 11.7MB** —— 详见技术方案 §5.1 |

**测试还抓到过一个 spec 歧义**：`natural_compare("01", "1")` 的方向在实现与测试之间不一致。
最终裁定"前导零多的排后面"（与 `strnatcmp` / 资源管理器一致），是**测试写错了**、不是实现错了。
这类歧义靠人眼 review 很难发现，是测试的直接价值。

### ⚠️⚠️ 最隐蔽的一个 bug：GUI 跑的其实是 Lua 解释器

**症状**：`batchsmith.exe` 启动正常、**退出码 0**，stdout 打印

```
Lua 5.4.7  Copyright (C) 1994-2024 Lua.org, PUC-Rio
>
```

**根因**：Lua 5.4.7 的源码树里有一个 **`onelua.c`** —— 单文件合并版，
它 `#include` 了全部源文件**并且自带解释器入口**。
最初的排除规则只写了 `lua.c` / `luac.c`，`onelua.c` 被 `*.c` 通配符捞了进去。

它进静态库后，在没人引用 Lua 时本是"死代码"。但 Windows 下这段合并代码会定义
**`WinMain`** —— 而 GUI 因为 `WIN32_EXECUTABLE ON`，入口点恰好就是 `WinMain`。
于是链接器从库里拉走 `onelua.c.obj` 来满足 `WinMain`，**程序入口点变成了 Lua REPL**。

**为什么难发现**：整条路径没有任何报错。链接成功、启动成功、退出码正常。

### ⚠️ 由此暴露的元问题：先前的冒烟判据是错的

之前判 GUI 是否正常，用的是这个（也是 `qt-qmake-windows-build` 技能里推荐的方法）：

```bash
timeout 10 ./batchsmith.exe ; echo $?     # 124 = 存活到超时 = 认为通过
```

得到 124 就判定通过。但 **Lua REPL 在终端里同样会一直等 stdin**，超时后同样是 124。
**这个判据无法区分"窗口起来了"和"REPL 在等输入"** —— 是一次彻底的假阳性。
我因此一度在文档里写下"GUI 启动存活 ✅"，而实际上 GUI 从未运行过。

**正确判据（四条叠加，缺一不可）**

```bash
# 1) 静态库内不得含任何带入口点的文件
ar t build/ci/third_party/liblua54.a | grep -E '^(lua|luac|onelua)\.c'   # 应为空

# 2) 二进制里不得有 Lua REPL 的符号与横幅字符串
nm build/ci/bin/batchsmith.exe | grep -iE 'pmain|print_version'          # 应为空
grep -c 'Copyright (C) 1994-2024 Lua.org' build/ci/bin/batchsmith.exe     # 应为 0

# 3) 关掉 stdin 后仍存活（REPL 遇 EOF 会立刻退出 rc=0）
timeout 6 ./build/ci/bin/batchsmith.exe < /dev/null; echo $?             # 期望 124

# 4) 最终判据：窗口真的存在
#    PowerShell: Get-Process batchsmith | Select-Object MainWindowTitle
#    期望 Title=[BatchSmith] Responding=True
```

实测第 4 条得到 `PID=22060 Title=[BatchSmith] Responding=True Threads=9`，
这才真正证明了 GUI 可用。

**修复**

```cmake
list(FILTER _lua_all_sources EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
```

并加了显式断言逐项复核文件名 —— 因为这条路径失败时是静默的，不能只靠正则。
Lua 的编译单元数从 34 降到 **33**（CMake 配置时会打印，可当作回归检查点）。

> 后来这个数字又降到 **32**：submodule 迁移带来的一颗"哑弹" `ltests.c` 也被排除了，
> 见下文 S6 的 [「另一个随 submodule 迁移混进来的东西」](#另一个随-submodule-迁移混进来的东西ltestsc)。

### 运行产物前的 PATH（构建目录不带运行时 DLL，直接双击会缺库）

| 来源 | 提供 |
|---|---|
| `C:/Qt/Tools/mingw1120_64/bin` | `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` |
| `C:/Qt/6.7.2/mingw_64/bin` | `Qt6Core/Gui/Widgets/Network/Svg.dll` |

> `ctest` 也需要这两条 —— 测试可执行文件同样依赖 Qt6Core 与 MinGW 运行时。
> 不加会看到 `0xc0000135`（STATUS_DLL_NOT_FOUND）。

**判读可执行文件是否正常启动**（GUI 程序用超时判断存活）

```bash
export PATH="/c/Qt/6.7.2/mingw_64/bin:/c/Qt/Tools/mingw1120_64/bin:$PATH"
timeout 10 ./build/windows-mingw/src/gui/batchsmith.exe; echo "RC=$?"
# 124 = 存活到超时（正常）；127 = 缺 DLL；其他非零 = 启动即崩
```

---

## S5 打包脚本（三平台）✅

**产出**

```
packaging/
├── package-windows.sh    -> BatchSmith-<版本>-windows-x64.zip
├── package-macos.sh      -> BatchSmith-<版本>-macos-universal.dmg
└── package-linux.sh      -> BatchSmith-<版本>-linux-x86_64.tar.gz
```

统一接口：`VERSION=x.y.z packaging/package-<平台>.sh <build-dir> <out-dir>`。

### Windows 脚本的实测结论

`windeployqt` 的四个开关是**实测**选出来的，不是照抄文档：

| 开关 | 省下 |
|---|---|
| `--no-opengl-sw` | `opengl32sw.dll` **19.7 MB**（Widgets 根本不走 OpenGL） |
| `--no-translations` | 翻译文件 4.8 MB |
| `--no-system-d3d-compiler` | `D3Dcompiler_47.dll` 4.0 MB |
| `--no-compiler-runtime` | VC 运行时（目标机通常已装） |

效果：默认部署 56.2 MB → 精简 27.8 MB → **zip 11.45 MB**。

**不假设 `windeployqt` 在 `PATH` 上。** 初版脚本直接调 `windeployqt`，等于押注
`install-qt-action` 会把 Qt 的 bin 加进 `PATH` —— 那是 CI 内部行为、本机无法复现，
一旦它变了，失败点会跑到流程**末尾**（打包阶段），白白多一轮 CI。
现在优先用 action 显式导出的 `QT_ROOT_DIR`（在 job 日志的 env 段里能看到它），
`PATH` 只作回退，两路都断就带明确提示退出。`package-macos.sh` 的 `macdeployqt` 同样处理
（那个脚本至今没在真机跑过，更不该有这类隐藏假设）。

三条分支都在本机实测过（用独立 fixture 目录区分"命中 `QT_ROOT_DIR`"与"命中 `PATH`"）：
`QT_ROOT_DIR` 有工具 → 命中它；未设置 → 回退 `PATH`；`QT_ROOT_DIR` 下没有 → 回退 `PATH`；
两路皆无 → 报错退出 1。

### ⚠️ 打包脚本踩到的坑（Git Bash 特有）

**症状**：`windeployqt` 只回一句

```
"\tmp\dist\BatchSmith\batchsmith.exe" does not exist.
```

**原因**：`windeployqt` 与 `7z` 都是**原生 Windows 程序**，不认 Git Bash 的 POSIX 路径。
直接传 `/tmp/dist/...` 会被 MSYS 拼成 `\tmp\dist\...` —— 既不是 POSIX 路径也不是有效的
Windows 路径，于是被判定为"文件不存在"。

**解法**：脚本开头用 `cygpath -m` 把输入目录归一化成「Windows 盘符 + 正斜杠」形式
（如 `C:/Users/.../dist`），MSYS 自带的工具与原生程序都能吃：

```bash
if command -v cygpath >/dev/null 2>&1; then
    BUILD_DIR="$(cygpath -m "$BUILD_DIR")"
    OUT_DIR="$(cygpath -m "$OUT_DIR")"
fi
```

**另一个坑**：不能假设 `7z` 在 PATH 上（GitHub runner 通常有，本机不一定）。
脚本现在按 `7z` → `7za` → `7zz` → `/c/Program Files/7-Zip/7z.exe` → `python zipfile`
的顺序回退，最后一级保证一定出得来包。

### macOS / Linux 脚本的现状

- **macOS**：`macdeployqt` + `hdiutil` 出 dmg。**产出未签名**，Gatekeeper 会拦；
  公开分发要另配 `codesign` + `notarytool`（Phase 8）。
- **Linux**：没有用 `linuxdeploy`（需额外下载 AppImage，本机无法预先验证），改为用 `ldd`
  捞出 Qt 相关动态库随包分发，配一个设置 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` 的启动脚本。
  零外部依赖、行为可推导；代价是没有 AppImage 那样单文件。AppImage 列为 Phase 8 改进项。

**验证程度**：Windows 脚本已在本机**端到端实跑**（11.45 MB zip、29 个条目、路径无异常）。
macOS 与 Linux 脚本只做了 `bash -n` 语法检查，**未在真机跑过** —— 它们必须在对应平台验证。

---

## S6 CI：三平台构建 + 打包 + 发版 🔄 首轮失败已修复，待复验

`.github/workflows/ci.yml`。三个作业：`build`（矩阵）、`release`（打 tag 时）、`format`。

**矩阵**

| 平台 | Qt arch | 备注 |
|---|---|---|
| windows-latest | `win64_msvc2022_64` | CI 用 MSVC，本机开发用 MinGW —— 顺带覆盖两种 Windows 工具链 |
| macos-latest | `clang_64` | `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"` 出 universal |
| ubuntu-latest | `linux_gcc_64` | 需装系统依赖才能链接 Qt6Widgets |

**流程**：checkout（`submodules: recursive`）→ 装 Qt → 装 Ninja → 配置 → 构建 → 测试 → 打包 → 上传 artifact。

**发版**：push `v*` tag 时，`release` 作业下载三平台 artifact，用 `gh release create`
建 Release 并附上产物（`--generate-notes` 自动生成说明）。重跑同一 tag 时改为
`gh release upload --clobber`，所以整体是幂等的。

### 为 CI 专门做的两处硬化（都是在本机模拟 CI 时暴露出来的）

1. **Ninja 不假设 runner 自带** —— 本机用 `cmake --preset ci` 模拟时，直接报
   `unable to find a build program corresponding to "Ninja"`。现在统一用
   `pip install ninja` 并把它的 bin 目录写进 `$GITHUB_PATH`，三平台同一条命令、结果确定。
2. **Windows 需要显式准备 MSVC 环境** —— Ninja 生成器不像 VS 生成器那样会自己去注册表
   找 MSVC，`cl.exe` 不在 runner 默认 PATH 上，所以加了 `ilammy/msvc-dev-cmd`。

### 还需要注意

- macOS 产物**必须在 macOS 上构建**，公开分发还要签名与公证。
- 三平台都要真机冒烟验证。**"只在 Windows 开发完直接编其他平台"是 Qt 项目的经典翻车点。**
- 首次推送后三平台结果差异极大（见下节），**不能因为本机是绿的就把 CI 当绿的**。

### 首次推送后的真实结果（2026-09-14）

推送 `e126e66` 后 CI 自动跑了一轮，三平台结果如下：

| Job | 结果 | 失败位置 |
|---|---|---|
| 代码格式 | ✅ | — |
| Linux x86_64 | ✅ **全绿**（含打包与上传） | — |
| Windows x64 | ❌ | 第 3 步「安装 Qt」——`install-qt-action` 找不到 `win64_msvc2022_64` |
| macOS universal | ❌ | 第 9 步「构建」——`ld: framework 'AGL' not found` |

顺带一个好收获：**Linux 全绿同时验证了 `package-linux.sh` 真的能用** ——
它此前只做过 `bash -n` 语法检查，没有真机跑过。

两个失败都与功能代码无关，但都必须在工程上解决。

#### ① Windows：`win64_msvc2022_64` 这个 Qt 包不存在

**这条是用与 CI 相同的工具复现确认的，不是推断。** 本机装同版本 aqt（`aqtinstall==3.3.0`，
与 workflow 里的 `aqtversion: ==3.3.*` 对齐）后：

```bash
python -m aqt list-qt windows desktop --arch 6.7.2
#  -> win64_llvm_mingw win64_mingw win64_msvc2019_64 win64_msvc2019_arm64

python -m aqt install-qt windows desktop 6.7.2 win64_msvc2022_64 --outputdir /tmp/x
#  -> ERROR : The packages ['qt_base'] were not found while parsing XML of package information!
#     与 CI 日志逐字一致

python -m aqt install-qt windows desktop 6.7.2 win64_msvc2019_64 --dry-run --outputdir /tmp/x
#  -> DRY RUN: 7 个包（qtbase/qtsvg/qtdeclarative/qttools/qttranslations/d3dcompiler_47/opengl32sw）
```

另外抓官方清单核对过（`download.qt.io/.../qt6_672/Updates.xml`）：Qt 6.7.2 的 Windows
包只有 `win64_msvc2019_64` / `win64_msvc2019_arm64` / `win64_mingw` / `win64_llvm_mingw`，
**没有 `msvc2022_64`** —— 那个后缀到 **6.8.0** 才出现（`aqt list-qt ... --arch 6.8.0`
可以对照）。也就是说失败发生在下载 Qt 之前，与工作区的任何一行代码无关。
已改为 `win64_msvc2019_64`（与 VS2022 的 ABI 兼容，`windows-latest` 直接可用），
并用 `--dry-run` 验证过这个包确实能装。

> `The packages ['qt_base'] were not found` 就是"arch 名在仓库里不存在"的标准症状 ——
> 看到它别去查 `qt_base`，去查 arch 名字。

#### ② macOS：`-framework AGL` 来自 Qt 自己

**根因不在我们这边。** `Qt6::Gui` 的公开链接依赖里带着 AGL，来源是 Qt 自带的
`FindWrapOpenGL.cmake`（非商业发行版在 6.9 之前都含这段）：

```cmake
find_library(WrapOpenGL_AGL NAMES AGL)
if(WrapOpenGL_AGL)
    set(__opengl_agl_fw_path "${WrapOpenGL_AGL}")
endif()
if(NOT __opengl_agl_fw_path)
    set(__opengl_agl_fw_path "-framework AGL")   # ← 问题所在
endif()
target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE ${__opengl_agl_fw_path})
```

AGL 是 Carbon 时代的 OpenGL 垫片，已从 macOS 26（Tahoe）的 SDK 中移除。
在旧 SDK 上 `find_library` 能拿到真实路径，所以这个缺陷潜伏了很多年；
一旦 SDK 里没有 AGL，它就回退成**字面量** `-framework AGL`，
于是任何链接 `Qt6::Gui` 的程序都在链接期失败。Qt 官方在 6.9 里把整段删掉了。

**做法**：在 `cmake/FindWrapOpenGL.cmake` 放一份 Qt 该模块的副本（BSD-3-Clause 版权
声明按原样保留），**只删掉 AGL 那一段**，与上游 6.9 的修复逐行对应；顶层
`CMakeLists.txt` 用 `list(PREPEND CMAKE_MODULE_PATH ...)` 让我们的副本排在 Qt 自带副本之前。

**为什么这样能生效 —— 本机实测，不是推测。** 关键在于 `Qt6Config.cmake` 是用
`list(APPEND ...)` 追加自己的模块目录的，所以 PREPEND 进来的 `cmake/` 一定在前面。
用 `cmake --debug-find-pkg=WrapOpenGL` 能直接看到结论：

```
find_package considered the following paths for FindWrapOpenGL.cmake:
  C:/Qt/Tools/CMake_64/share/cmake-3.29/Modules/FindWrapOpenGL.cmake
The file was found at
  D:/Development/BatchSmith/cmake/FindWrapOpenGL.cmake
```

与 Qt 原版逐行 diff 过，**实质差异只有被删掉的 AGL 那 8 行**（加一段说明注释）。

**同时加了一道配置期断言**：`find_package(Qt6)` 之后检查 `WrapOpenGL::WrapOpenGL`
的链接接口，若仍有 AGL 就立刻 `FATAL_ERROR`。因为这个方案的全部依仗就是"模块搜索顺序"，
一旦有人把 PREPEND 改成 APPEND，症状会退化成十几分钟一次的 CI 往返 + 一条离根因很远的
`ld` 报错。断言把这件事变成 5 秒钟的配置期错误（两条分支都在本机用伪目标验过）。

**顺带硬化**：`-Werror` 现在只在**已验证过警告集**的编译器上开启（目前只有 GCC）。
MSVC 的 `/W4` 与 AppleClang 各有一批自己的告警，给它们改成"警告照打、不阻断构建" ——
CI 的红应该只反映真实问题，而不是"某个编译器恰好多了一条告警"。

#### 另一个随 submodule 迁移混进来的东西：`ltests.c`

首次 CI 日志里出现了 `Building C object ...third_party/.../lua/ltests.c.o`。
`ltests.c` 是 Lua 内部的测试模块（`luaB_opentests` / `lua_checkmemory` 等），
**`lua.org` 官方发行 tarball 的 `src/` 里没有它 —— 它是 `lua/lua` 这个 GitHub 镜像
仓库多带的文件**，随 submodule 迁移悄悄进了编译列表。

它在静态库里是一颗"哑弹"：静态库只按需拉取目标文件，所以它既不会被链进产物、
也不会报错；但它对外暴露一批符号，并且引用了 `ltable.c` 只在 `LUA_DEBUG` 下才编译的
函数（`luaH_getnode` 等）—— 哪天有谁把它拉进来，就是一堆未定义符号。

现在排除列表从 3 个变成 4 个（`lua.c` / `luac.c` / `onelua.c` / `ltests.c`），
Lua 编译单元数 **34 → 32**，配置阶段会打印这个数字。

---

## S7（已移出本文）静态 Qt 构建管线

原 S7 的内容已移到 [`项目阶段规划.md`](项目阶段规划.md) 的 **Phase 8**。

简要结论留在这里备查：许可上**已经没问题**（项目是 GPL-3.0，可按 GPLv3 使用 Qt 并自由
静态链接，LGPLv3 下的重新链接义务不适用，见技术方案 ADR-10）；但工程量不小
（`-static` 配置 + 静态 OpenSSL/ICU/harfbuzz/freetype，Windows 还须 `/MT` 静态 CRT，
且每个平台各做一遍）。在功能没跑通之前做它性价比为负，所以放到最后。
