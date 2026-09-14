# 变更记录

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

> 项目按 P1–P8 阶段推进，见 [`docs/项目阶段规划.md`](docs/项目阶段规划.md)。
> **0.x 期间不保证接口稳定**：预设格式、DSL 语义、CLI 参数都可能变。

## [未发布]

### 待定（需要拍板，尚未实施）

- **多列表区段的默认展开规则**。当前按技术方案 §3.2 取**笛卡尔积**
  （`$list1$-$list2$` → 3×3 = 9 行）；已提出改为**默认逐行对应（zip）**
  （`$list1$-$list2$` → 3 行，短列表按空串补位）。**改动暂缓，结论未定**。
  若要改，涉及技术方案 §3.2（标注为「定稿」）、`docs/界面说明.md`、
  `dsl::evaluate_simple` 及其两处测试；笛卡尔积则改为只经 `matrix()` helper 提供。

## [0.2.0] — 2026-09-14

主界面落地，并打通 core 的模板扫描与求值链路。

### 新增

- **主界面（上下两段）**：上半是水平滚动的列表区——N 列并排、每列内部竖向滚动、
  列可增删（`＋ 添加列表` / 列标题右侧 `×`）；下半是单行表达式输入 + 右侧「确定」，
  其下是输出列表。
- **`batchsmith_gui` 静态库**：界面代码从可执行文件里拆出来，使其能被测试链接；
  `batchsmith` 只剩一个瘦 `main.cpp`。
- **core 求值链路**：`list/ListSource`（列表源）、`dsl/template_scanner`
  （`$...$` 分段、`\$` 转义、未配对 `$` 报错、失败时不返回半截结果）、
  `dsl/simple_evaluator`（**临时求值器**，见「说明」）。
- **GUI 离屏测试**：8 用例 / 76 断言，`QT_QPA_PLATFORM=offscreen`，覆盖主窗口装配、
  增删列与编号规则、回车与点「确定」等价、报错时清空输出。
- **文档与截图**：[`docs/界面说明.md`](docs/界面说明.md)（版面、交互契约、三条自定规则）、
  `docs/images/ui-main.png` 等界面截图。
- **截图工具**：`BATCHSMITH_UI_SHOT=<path>` 可用测试目标把界面渲染成 PNG
  （位于 `tests/`，不给发布产物增加只服务于截图的开关）。

### 变更

- 版本 **0.1.0 → 0.2.0**。
- GUI 测试单独一个目标：`batchsmith_tests` 仍只链 core（无图形依赖，可在无显示服务
  的环境跑）。
- `.gitattributes`：`third_party/**` 的 `-diff` / `linguist-vendored` 收窄到四个
  submodule 目录，不再误伤我们自己写的 `third_party/CMakeLists.txt` 与 `README.md`。

### 修复

- **macOS 链接失败 `ld: framework 'AGL' not found`**：Qt 6.9 之前，非商业发行版自带的
  `FindWrapOpenGL.cmake` 会把已从 macOS 26（Tahoe）SDK 移除的 AGL 写进 `Qt6::Gui`
  的公开链接接口。现以项目内的 `cmake/FindWrapOpenGL.cmake` 覆盖该模块
  （仅删 AGL 段，与上游 6.9 的修复逐行对应），并加配置期断言兜住模块搜索顺序这个前提。
- **CI：Windows 的 Qt arch** 改为 `win64_msvc2019_64`——Qt 6.7.2 的 Windows 包不存在
  `win64_msvc2022_64`（该后缀 6.8 才出现），原来的写法在下载 Qt 之前就失败。
- **Lua 静态库排除 `ltests.c`**：它是 `lua/lua` 镜像仓库比官方 tarball 多带的内部测试
  模块，引用了 `LUA_DEBUG` 下才编译的符号。编译单元 34 → 32。
- **打包脚本**不再假设 `windeployqt` / `macdeployqt` 在 `PATH` 上：优先用
  `install-qt-action` 显式导出的 `QT_ROOT_DIR`，再回退 `PATH`，两路都断则带提示退出。
- **CI artifact** 只上传打包好的归档，不再把「暂存目录 + 归档」两份重复内容一起上传
  （Windows artifact 实测 36.5MB → 与 zip 同量级）。

### 说明：`dsl::evaluate_simple` 是临时实现

它只支持「区段里写列表名」这一子集（`$list1$`、`mv $list1$ out`）；
helper、索引（`$list1[1]$`）、`#list1`、`$i$` / `$rows$` 一律**明确报错**并指向 Phase 2。

理由是技术方案 ADR-5 定的是**单运行时**：现在就让 core 长出一个自成一套语义的求值器，
正是 ADR-5 想避免的双语义漂移。它存在的目的只是让界面能演示「输入 → 输出」这条链路。
**Phase 2 的 Lua 编译器落地时必须删除它**，并让它现在这组用例继续通过——
那组用例同样是「行展开规则」的规格说明。

## [0.1.0] — 2026-09-13

P1（POC / 技术验证）完成。目标是先证明三件事：技术栈能在三平台跑通、产物体积可接受、
Lua 沙箱所需原语确实可用。

### 新增

- 工程骨架：CMake + Qt 6 Widgets + 四个 submodule 依赖（Lua 5.4 / sol2 / toml++ / doctest）
- 三个构建目标：`batchsmith_core`（只依赖 QtCore）、`bs`（CLI）、`batchsmith`（GUI）
- 构建链路：CMakePresets（本机与 CI 各一套）、Debug/Release
- 测试链路：doctest + ctest（10 用例 / 246 断言）
- CI：三平台矩阵（构建 + 测试 + 打包 + artifact），打 `v*` tag 自动发 Release
- 打包脚本：`packaging/package-{windows,macos,linux}.sh`
- 首个真实功能：自然排序比较器 `natural_compare`（`file2` 排在 `file10` 之前）
- 文档：技术方案与实现路线（ADR-1–10、DSL 规格）、项目阶段规划、Phase 1 施工记录

> 本文件不引用 git tag 链接：项目尚未打任何 tag。等真正发版打上 `v*` 之后，
> 再把这里的版本号补成可点击的 compare 链接。
