# AGENTS.md — QTrans

面向在本仓库中工作的 AI 编码代理的说明。

## 项目

QTrans 是基于 **C++17 / Qt6** 的桌面应用，使用 Hy-MT（llama.cpp）进行**本地推理**。全平台 CPU；**仅 Windows x64 Release** 可附带 `ggml-vulkan.dll` 做 Vulkan GPU 卸载（无 CUDA）。功能包括：翻译/回译、模型下载、划词翻译（平台热键 + 弹窗）。

| 项 | 说明 |
|------|--------|
| 构建 | CMake 3.21+、Ninja、**vcpkg**（`VCPKG_ROOT`） |
| UI | Qt6 Widgets |
| 推理 | 通过 vcpkg `llama-cpp` 使用 llama.cpp |
| 许可 | GPL-3.0 |

用户文档：[README.md](README.md)、[docs/README_zh.md](docs/README_zh.md)。  
开发者文档：**[docs/develop/](docs/develop/)**（工作流、构建、CI、发布）。

## 仓库结构

```text
src/
  app/           # 主窗口、UI 页面、任务胶水层、单实例、Qt 字符串桥接
  text/          # UTF-8（simdutf）、ICU 分句、按 token 预算分块
  log/           # spdlog 初始化、组件日志、AI trace 落盘、控制台进度
  translation/   # Hy-MT、推理引擎、语言列表
  model/         # 模型目录、推理后端解析、平台默认模型
  network/       # 模型下载（libcurl）
  storage/       # 路径、设置
  task/          # 队列、编排器（工作线程）
  wordselect/    # 热键、剪贴板、弹窗、会话；mac/ win/ 平台代码
.github/workflows/   # CI + 发布（YAML 位于 workflows/ 根目录，不要放子目录）
docs/develop/    # 贡献者文档
resources/       # Qt 资源（图标）
```

`qtrans_engine` 静态库：`src/` 下除 app 壳与 wordselect UI 接线外的核心逻辑。可执行目标：`QTrans`。

## 存储与翻译

- **AppPaths**（`src/storage/app_paths.h`）：便携模式使用 `<app>/data/`；系统模式使用 `~/.qtrans/`。子目录：`models/`、`settings/`、`logs/`。通过 `AppPaths::detect` + `ensureDirectories()` 解析路径；不要将应用数据或诊断信息写入进程 cwd。
- **日志**（`src/log/`）：spdlog 组件化日志。Debug：控制台 trace + 旋转 `logs_dir/qtrans.log`。Release：仅控制台 warn/error（无文件 sink）。
- **AI trace**（`src/log/ai_trace.*`）：仅在 **Debug** 构建中将 prompt/response 写入 `logs_dir/ai_output_*.log`。Release 构建为空操作。
- **UTF-8 / 分块**（`src/text/`）：simdutf 负责校验与边界；ICU 仅用于分句。主窗口翻译与回译在输入超出上下文时自动分块。**划词翻译**必须返回明确错误，不得自动分块。
- **Qt 边界**（`src/app/string_bridge.*`）：引擎调用的唯一 `QString` ↔ UTF-8 `std::string` 转换入口。

## 本地构建

```bash
# Debug + compile_commands.json（clangd）
cmake --preset default && cmake --build --preset debug

# Release
cmake --preset arm64-osx-release && cmake --build --preset arm64-osx-release     # macOS arm64（CPU）
cmake --preset x64-mingw-release && cmake --build --preset x64-mingw-release     # Win x64（+ ggml-vulkan.dll）
cmake --preset arm64-mingw-release && cmake --build --preset arm64-mingw-release  # WOA arm64（CPU）
```

Preset 定义见 `CMakePresets.json`。`QTRANS_MULTI_BACKEND` 仅在 `x64-mingw*` triplet 启用。Win x64 Release 须将 `ggml-vulkan.dll` 与 `QTrans.exe` 同目录分发。首推默认模型：Win x64 → `hymt2-q4`；macOS arm64 / WOA → `hymt2-125bit`。新增依赖须同步更新 `vcpkg.json`。

## 测试

单元测试位于 `tests/`，按 `src/` 目录镜像（`task/`、`storage/`、`network/`、`model/`、`translation/`、`text/`、`log/`）。每个目录一个 gtest 可执行文件，链接 `qtrans_engine`。框架：vcpkg 提供的 **GoogleTest**（`vcpkg.json` 中的 `gtest`）。

```bash
cmake --preset arm64-osx-release-user -DQTRANS_BUILD_TESTS=ON
cmake --build --preset arm64-osx-release-user
ctest --test-dir build/arm64-osx-release-user --output-on-failure
ctest --test-dir build/arm64-osx-release-user -L dir:task   # 按目录过滤
```

默认 `QTRANS_BUILD_TESTS=OFF`。纯逻辑模块（解析器、状态机、INI I/O）在测试范围内；Qt 控件 / 平台 API 代码不在范围内。若需测试私有静态逻辑，在类头文件中添加 `friend struct XxxTestAccess;`，并在 `tests/<dir>/xxx_test_access.h` 中定义访问器。

向 `main` 提交的 PR 会在 CI 中运行完整 `ctest`（工作流 `unit-tests.yml`，任务 **Unit tests**）。

## 代码风格

- 使用仓库根目录 [`.clang-format`](.clang-format) 格式化（基于 Google，4 空格，指针右对齐）。
- 范围：`src/**/*.{cpp,h,mm}`。
- 建议提交前，确保修改过的 C++ 文件可通过：

```bash
clang-format -i <files>
```

CI 任务 **Code formatting** 会对发往 `main` 的 PR 运行 `clang-format-18`。

## Git 与 PR 工作流（必须遵守）

`main` 受保护：**禁止直接推送**。所有变更须通过 PR。

1. 分支：`users/<github-login>/<topic>`（例如 `users/touken928/fix-popup`）。
2. 仅在该分支上提交；切勿在 `main` 上提交。
3. 推送分支 → 向 `main` 开 PR。
4. CI 须通过：**Branch naming**、**Code formatting**。
5. 在 GitHub 合并；远程 head 分支会自动删除。本地执行：`git fetch --prune`。

**不要**执行 `git push origin main` 或在 `main` 上 `git commit`。

详情：[docs/develop/workflow.md](docs/develop/workflow.md)。

## CI 与发布

| 工作流 | 触发时机 |
|----------|------|
| `branch-policy.yml` | 推送非 `main` 分支、PR → `main` |
| `format-check.yml` | PR → `main` |
| `release.yml` | 仅 tag `v*` |

发布：在 `main` 上执行 `git tag vX.Y.Z && git push origin vX.Y.Z`（维护者操作）。

工作流文件**必须**位于 `.github/workflows/*.yml`（不要放在嵌套子目录）。

## 代理指南

1. **最小 diff** — 只改任务所需内容；不做无关重构，也不对在触文件之外的代码做顺手格式化。
2. **遵循现有模式** — 命名、Qt 父对象/所有权、signals/slots、include 顺序、`wordselect/mac|win/` 中的平台 `#if`。
3. **线程** — `TaskService` 运行在工作 `QThread` 上；UI 通过信号更新 `MainWindow` / 各页面。
4. **平台代码** — macOS：`src/app/mac/`、`src/wordselect/mac/`。Windows：`src/app/win/`、`src/wordselect/win/`。共享逻辑保持平台无关，放在 `src/`。
5. **禁止密钥** — 切勿在设置或代码中提交 API 密钥、token 或本地路径。
6. **避免 vcpkg 扰动** — 除非明确要求，不要升级 `vcpkg.json` / overlays。
7. **提交** — 仅在用户要求时提交；遵循仓库提交风格（`feat:`、`fix:`、`ci:` 等）。
8. **文档** — 面向贡献者的行为或工作流变更：更新 `docs/develop/` 下相关文件。

## 常见陷阱

- 添加 `Qt6::Network` 或额外 Qt 模块时未更新 `vcpkg.json` manifest。
- 将 GitHub 工作流放在 `.github/workflows/ci/`（GitHub 可能无法注册）。
- 使用 `users/<login>/...` 以外的分支名（CI 会失败）。
- 仅在本地修改 `main` 后尝试推送（Ruleset 会阻止）。

## 快速链接

- [工作流](docs/develop/workflow.md)
- [构建](docs/develop/build.md)
- [代码风格](docs/develop/code-style.md)
- [CI](docs/develop/ci.md)
- [分支保护](docs/develop/branch-protection.md)
- [发布](docs/develop/release.md)
