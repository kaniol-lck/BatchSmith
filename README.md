# BatchSmith

把**多个列表 + 一个输出表达式**编译成批量操作。配置一次预设，之后只需选择文件。

> 当前状态：**Phase 1（POC）已完成** —— 构建、测试、CI、打包链路全部跑通。
> core 只有版本信息与自然排序比较器，CLI 与 GUI 尚未接入功能。
>
> - 阶段路线图：[`docs/项目阶段规划.md`](docs/项目阶段规划.md)（Phase 1–8）
> - 技术选型与设计决策：[`docs/技术方案与实现路线.md`](docs/技术方案与实现路线.md)（ADR-1–10）
> - Phase 1 施工细节与踩坑记录：[`docs/Phase1-工程搭建记录.md`](docs/Phase1-工程搭建记录.md)

## 这是什么

解决的是下面这类活：

- 文件批量重命名（番剧、照片、素材）
- 快速生成组合配对名称（多列表笛卡尔积）
- 把文件夹内容当列表做批量处理或统计
- 生成可核对、可撤销的批量命令

既有工具里，批量重命名工具（Bulk Rename Utility、Advanced Renamer、PowerRename、f2）解决了"改名字"但不做"构造名字"；命令行工具（`parallel --link`、`paste`+`awk`、`xargs -P`）能构造名字但门槛高、缺预览、缺撤销。本项目落在两者之间。

**设计中心是"预设"这一形态，不是列表界面本身。**

## 技术选型

| 项 | 选择 |
|---|---|
| 语言 | C++20 |
| GUI | Qt 6 **Widgets**（原生控件，不用 QML，不用 WebView） |
| 表达式运行时 | Lua 5.4（PUC Lua，**不用 LuaJIT**） |
| Lua 绑定 | sol2 |
| 构建 | CMake ≥ 3.21 + Ninja |
| 测试 | doctest（core）/ QTest（GUI，待 M5） |
| 许可 | GPL-3.0 |

几个决定背后的理由（详见技术方案 ADR）：

- **不用 LuaJIT**：它的 JIT 轨迹不触发 `LUA_MASKCOUNT` 钩子，死循环一旦被 JIT 编译就拦不住，与沙箱硬要求冲突。
- **不用 Qt Quick**：本项目是密集数据型桌面工具，没有动画/触屏/3D 需求，而 Widgets 提供最完整的 Model/View 与传统控件。
- **静态链接 Qt 是合法的**：项目为 GPL-3.0，可按 GPLv3 使用 Qt 并自由静态链接（LGPLv3 下才有重新链接义务）。代价是项目许可被锁定在 GPL 系。

## 依赖

第三方依赖（Lua / sol2 / toml++ / doctest）通过 **git submodule** 管理，锁在各自的 release tag 上。
第三方代码不进本仓库的提交历史，仓库自身体积约 300 KB。

**克隆时必须带 `--recursive`：**

```bash
git clone --recursive <仓库地址>

# 忘了加、或已经有本地副本时：
git submodule update --init --recursive
```

漏了这一步，CMake 会在配置阶段直接报错并提示上面这条命令，不会让你在编译错误里猜。
版本清单与升级方式见 [`third_party/README.md`](third_party/README.md)。

需要的外部环境只有：

- CMake ≥ 3.21、Ninja
- 一个支持 C++20 的编译器（MSVC 2022 / GCC 11+ / Clang 14+）
- Qt 6.5 及以上，含 Core / Gui / Widgets

## 构建

### 本机（Windows + Qt 6.7.2 + MinGW）

仓库里带了 `CMakeUserPresets.json`（已 gitignore，不进库），直接：

```bash
cmake --preset windows-mingw
cmake --build --preset windows-mingw
ctest --preset windows-mingw
```

### 其他环境

`CMakePresets.json` 里的 `default` 预设需要环境变量 `QT_ROOT` 指向 Qt 安装前缀：

```bash
export QT_ROOT=/path/to/Qt/6.7.2/gcc_64     # Linux
# export QT_ROOT=~/Qt/6.7.2/macos           # macOS

cmake --preset default
cmake --build --preset default
ctest --preset default
```

### 常用预设

| 预设 | 用途 |
|---|---|
| `default` | Debug，全量构建 |
| `release` | Release + 严格警告 |
| `dev-fast` | 只编 core + CLI，跳过 GUI 与测试（改 core 时用） |
| `ci` | CI 使用，由 `install-qt-action` 提供 Qt 前缀 |

## 运行

**CLI**（`bs`）——需要 Qt 与 MinGW 的运行时库在 `PATH` 上：

```bash
export PATH="/c/Qt/6.7.2/mingw_64/bin:/c/Qt/Tools/mingw1120_64/bin:$PATH"
./build/windows-mingw/bin/bs.exe --version
```

**GUI**（`batchsmith`）——同样需要上述 `PATH`：

```bash
./build/windows-mingw/bin/batchsmith.exe
```

所有可执行文件都统一输出到 `<构建目录>/bin/`，与生成器无关（见顶层 `CMakeLists.txt` 的说明）。

> 构建目录里不会自动带运行时 DLL，手工双击会缺库。
> 要一份可分发的完整包，直接用打包脚本：
>
> ```bash
> VERSION=0.1.0 bash packaging/package-windows.sh "$PWD/build/windows-mingw-release" "$PWD/dist"
> ```
>
> 详见 [`docs/Phase1-工程搭建记录.md`](docs/Phase1-工程搭建记录.md) 的 S5。

## 目录结构

```
BatchSmith/
├── CMakeLists.txt            顶层工程
├── CMakePresets.json         可移植的构建预设（committed）
├── CMakeUserPresets.json     本机工具链路径（gitignored）
├── docs/                     技术方案与搭建步骤
├── third_party/              vendored 依赖：Lua / sol2 / toml++ / doctest
├── src/
│   ├── core/                 batchsmith_core 静态库 —— 只依赖 Qt6::Core
│   ├── cli/                  bs
│   └── gui/                  batchsmith（Qt 6 Widgets）
├── presets/                  示例预设
└── tests/                    单元测试
```

`src/core` **只依赖 QtCore**，不引用 QtGui/QtWidgets。这样 core 可以在无图形环境下单测，也为将来更换 GUI 框架留出余地。

## 安全模型（摘要）

执行是不可逆的那一半，所以：

- **默认 dry-run**，没有显式确认不产生任何副作用；
- 外部命令以 **argv 数组**传递（`QProcess::start`），**绝不经 shell 字符串拼接**；
- 重命名前做冲突检测（两条输出同名 / 目标已存在 / 源不存在 / 批内互换走两阶段重命名）；
- 文件行**绑定绝对路径而不是序号**，避免目录变动后命令打到别的文件上；
- 每次执行写 JSONL 撤销日志，可逆序回放；
- 预设里的 Lua 在沙箱中执行：`_ENV` 白名单 + 指令数钩子 + 内存上限 + 墙钟超时。

## 许可

GPL-3.0，见 [LICENSE](LICENSE)。

所选依赖（Lua、sol2、toml++、doctest、Qt）的许可均与 GPL-3.0 相容。
