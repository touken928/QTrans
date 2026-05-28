# GitHub 配置

## `workflows/`

| 目录 | 用途 |
|------|------|
| `ci/` | 分支命名（`branch-policy.yml`）、`main` 上的格式检查（`format-check.yml`） |
| `release/` | 打 tag 后的多平台构建与 GitHub Release 发布 |

## 本地格式化

仓库根目录的 [`.clang-format`](../.clang-format) 为统一风格。修复格式：

```bash
while IFS= read -r -d '' f; do clang-format -i "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

`branch-policy` 在 push 到非 `main` 分支（或 PR 进 `main`）时校验 `users/<GitHub 用户名>/<主题>`。

GitHub Actions 会递归加载 `workflows/` 下任意子目录中的 workflow 文件。
