# Git 与协作流程

## 分支约定

功能开发一律使用：

```text
users/<GitHub 用户名>/<主题>
```

示例：`users/touken928/fix-format-check`

- `<主题>` 使用简短英文或拼音，描述清楚即可
- 中间一段 **必须** 与推送者 / PR 作者的 GitHub 登录名一致

`main` 为稳定分支，**不要** 在 `main` 上直接提交。

## 日常开发

```bash
git checkout main
git pull origin main

git checkout -b users/<用户名>/<主题>
# 编辑、commit；提交前建议运行 clang-format（见 code-style.md）

git push -u origin users/<用户名>/<主题>
```

在 GitHub 创建 Pull Request：**base = `main`**，**compare = `users/...`**。

等待 CI 全部通过（见 [ci.md](ci.md)）后，在网页上 **Merge**。

```bash
git checkout main
git pull origin main
```

## 禁止的操作

| 操作 | 说明 |
|------|------|
| `git push origin main`（直接推提交） | 由 Ruleset 禁止，须走 PR |
| 在 `main` 上 `git commit` 再推送 | 同上 |
| 使用 `feature/xxx` 等非 `users/...` 分支名 | 由 CI **Branch naming** 拒绝 |

## 合并后清理分支

仓库已开启 **Automatically delete head branches**：PR 合并后 **远程** 功能分支会自动删除。

本地手动清理：

```bash
git fetch --prune
git branch -d users/<用户名>/<主题>
```

## 首次向仓库贡献

1. Fork 或取得写权限（视仓库设置）
2. 按上文创建 `users/<你的用户名>/...` 分支
3. 开 PR 并等待 review / CI
4. 合并后 `git pull` 同步本地 `main`
