# 持续集成（CI）

Workflow 文件位于 [`.github/workflows/`](../../.github/workflows/) **根目录**（勿放入子目录，否则 GitHub 可能无法识别）。

## 检查一览

| Workflow 文件 | Job 名称（Ruleset 中搜索） | 触发条件 |
|---------------|---------------------------|----------|
| `branch-policy.yml` | **Branch naming** | push 到非 `main` 分支；PR → `main` |
| `format-check.yml` | **Code formatting** | PR → `main` |
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

## Release

打 tag `v*`（如 `v0.2.1`）后构建 macOS / Windows 产物并发布到 [GitHub Releases](https://github.com/touken928/QTrans/releases)。不参与 PR 合并门禁。详见 [release.md](release.md)。

## 手动触发

`branch-policy.yml` 与 `format-check.yml` 支持 **workflow_dispatch**，可在 Actions 页手动 Run workflow（用于首次在 Ruleset 中登记检查名）。

## 查看结果

- PR 页面 **Checks** 标签
- [Actions](https://github.com/touken928/QTrans/actions)

## 与分支保护的关系

若 `main` 的 Ruleset 启用了 **Require status checks**，须勾选 **Branch naming** 与 **Code formatting**（名称须与 Job 完全一致）。配置说明见 [branch-protection.md](branch-protection.md)。
