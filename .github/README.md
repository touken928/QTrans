# GitHub 配置

## 开发流程（PR 合并进 `main`）

`main` 受保护：**禁止直接 push**，只能通过 Pull Request 合并。

```bash
git checkout main && git pull
git checkout -b users/<GitHub用户名>/<主题>
# 改代码、commit；提交前建议 clang-format
git push -u origin users/<GitHub用户名>/<主题>
```

在 GitHub 上开 PR：`users/...` → `main`，等待 CI 通过后点 **Merge**。

```bash
git checkout main && git pull
```

### 合并后删除分支

在 **Settings → General → Pull Requests** 中开启 **Automatically delete head branches**，PR 合并后 GitHub 会删除远程功能分支（如 `users/.../主题`）。

本地清理已合并分支：

```bash
git fetch --prune
git branch -d users/<GitHub用户名>/<主题>
```

发版：`git tag v1.0.0 && git push origin v1.0.0`（触发 `release/` workflow）。

## `workflows/`

| 目录 | 用途 |
|------|------|
| `ci/` | PR 检查：分支命名、代码格式 |
| `release/` | 打 tag 后的多平台构建与 GitHub Release |

| Workflow | 检查名（Ruleset ADD checks） | 触发 |
|----------|------------------------------|------|
| `branch-policy.yml` | **Branch naming** | push 非 `main`；PR → `main` |
| `format-check.yml` | **Code formatting** | PR → `main` |

## 本地格式化

```bash
while IFS= read -r -d '' f; do clang-format -i "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

## Ruleset（保护 `main`）

见仓库 Wiki 或团队文档；首次配置前到 **Actions** 手动 Run 一次 workflow，或 push 功能分支并开 PR，以便 **ADD checks** 出现 **Branch naming** / **Code formatting**。
