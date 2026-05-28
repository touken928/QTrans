# 分支保护（Ruleset）

`main` 通过 GitHub **Repository rules → Rulesets** 保护。以下为推荐配置（PR 合并进 `main`，禁止直推）。

## Target branches

- 包含：`main`

## 建议启用

| 规则 | 说明 |
|------|------|
| **Require a pull request before merging** | 禁止直接 push `main`，必须 PR 合并 |
| **Require status checks to pass** | 合并前 CI 须通过 |
| → **Branch naming** | 在 ADD checks 搜索框添加 |
| → **Code formatting** | 同上 |
| **Block force pushes** | 禁止对 `main` force push |
| **Restrict deletions** | 禁止删除 `main` |
| **Do not require status checks on creation** | 建议开，避免新建分支被误拦 |

### Require PR 子项（按需）

- **Required approvals**：单人仓库可设为 `0` 或 `1`
- **Dismiss stale reviews**：建议开

### 可选

| 规则 | 说明 |
|------|------|
| **Require branches to be up to date before merging** | 多人并行开发时可开，要求 PR 基于最新 `main` 并重跑 CI |

## 建议不要启用

| 规则 | 原因 |
|------|------|
| **Restrict updates** | 会阻止所有对 `main` 的 push，与「仅禁止直推、允许 PR 合并」重复且易误配 |
| **Restrict creations** | 与 `main` 保护无关 |
| **Require linear history** | 若使用 Merge commit 合并 PR 可能冲突 |
| **Require signed commits** | 未统一 GPG 签名时不要开 |

## ADD checks 为空时

1. 确认 [Actions](https://github.com/touken928/QTrans/actions) 中 **Branch naming** / **Code formatting** 至少成功运行过一次  
2. 在 ADD checks **搜索框** 输入 Job 名，不要等待空列表自动填充  
3. 确认 workflow 在 `.github/workflows/` 根目录（见 [ci.md](ci.md)）

## 合并后删分支

**Settings → General → Pull Requests → Automatically delete head branches**（仓库已启用）：PR 合并后自动删除远程 `users/...` 分支，不影响 `main`。

## 相关文档

- 日常流程：[workflow.md](workflow.md)
- CI 说明：[ci.md](ci.md)
