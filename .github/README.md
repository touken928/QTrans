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

发版：`git tag v1.0.0 && git push origin v1.0.0`（触发 `release.yml`）。

## `workflows/`

Workflow 文件须放在 `.github/workflows/` **根目录**（不要放在子目录，否则 GitHub 可能无法识别）。

| 文件 | 检查名（Ruleset ADD checks） | 触发 |
|------|------------------------------|------|
| `branch-policy.yml` | **Branch naming** | push 非 `main`；PR → `main` |
| `format-check.yml` | **Code formatting** | PR → `main` |
| `release.yml` | — | tag `v*` |

## 本地格式化

```bash
while IFS= read -r -d '' f; do clang-format -i "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

## Ruleset（保护 `main`）

CI 至少成功跑过一次后，在 **ADD checks** 中搜索 **Branch naming**、**Code formatting** 并添加。
