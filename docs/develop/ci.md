# 持续集成（CI）

Workflow 文件位于 [`.github/workflows/`](../../.github/workflows/) **根目录**（勿放入子目录，否则 GitHub 可能无法识别）。

## 检查一览

| Workflow 文件 | Job 名称（Ruleset 中搜索） | 触发条件 |
|---------------|---------------------------|----------|
| `branch-policy.yml` | **Branch naming** | push 到非 `main` 分支；PR → `main` |
| `format-check.yml` | **Code formatting** | PR → `main` |
| `build-macos.yml` | **Unit tests** | PR → `main`；push `main`（缓存预热）；Release 复用 |
| `build-windows-msvc.yml` | **MSVC build and tests** | PR → `main`；push `main`；Release 复用 |
| `release.yml` | （无 PR 检查） | push tag `v*` |

Dependabot 的提交会跳过 **Branch naming**。

## Branch naming

校验分支名：

```text
users/<GitHub 用户名>/<非空主题>
```

且 `users/` 后第一段与推送者或 PR 作者一致。

## Code formatting

对 `src/` 下 C/C++/ObjC++ 源文件执行 `clang-format --dry-run --Werror`。

## Unit tests

`build-macos.yml` 在 `macos-14` 上使用 preset `arm64-osx-release` 配置并执行全量 `ctest`。`build-windows-msvc.yml` 另外使用 `x64-msvc-static-release` 与静态 vcpkg triplet 构建并运行同一测试集，防止 Windows/MSVC 兼容性退化。

合并到 `main` 后会再跑一次对应 workflow，将 vcpkg binary artifacts 写入 **default branch** 作用域，后续 PR（未改 `vcpkg.json` / overlay 时）可命中缓存。依赖变更后首次仍会冷启动，之后可通过 triplet 对应的 `restore-keys` 部分复用。

CI 与 Release 共用相同的 vcpkg binary cache。`vcpkg_installed` 不直接缓存，每次均由 `vcpkg install` 根据当前工具链和 ABI 重新验证并恢复，避免 runner 或编译器更新后误用旧安装树。
vcpkg baseline 检出、binary cache 与 manifest 安装统一由 `.github/actions/setup-vcpkg/action.yml` 维护；平台 workflow 只负责工具链、配置、构建及测试/打包。

## Release

打 tag `v*`（如 `v0.2.1`）后构建 macOS / Windows 产物并发布到 [GitHub Releases](https://github.com/touken928/QTrans/releases)。不参与 PR 合并门禁。详见 [release.md](release.md)。

## 手动触发

`branch-policy.yml`、`format-check.yml`、`build-macos.yml` 与 `build-windows-msvc.yml` 支持 **workflow_dispatch**，可在 Actions 页手动 Run workflow（用于首次在 Ruleset 中登记检查名）。

## 查看结果

- PR 页面 **Checks** 标签
- [Actions](https://github.com/touken928/QTrans/actions)

## 与分支保护的关系

若 `main` 的 Ruleset 启用了 **Require status checks**，须勾选 **Branch naming**、**Code formatting**、**Unit tests** 与 **MSVC build and tests**（名称须与 Job 完全一致）。配置说明见 [branch-protection.md](branch-protection.md)。
