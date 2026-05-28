# GitHub 配置

## `workflows/`

| 目录 | 用途 |
|------|------|
| `ci/` | PR 检查：分支命名（`branch-policy.yml`）、代码格式（`format-check.yml`） |
| `release/` | 打 tag 后的多平台构建与 GitHub Release 发布 |

## 本地格式化

仓库根目录的 [`.clang-format`](../.clang-format) 为统一风格。修复格式：

```bash
while IFS= read -r -d '' f; do clang-format -i "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

在 Ruleset 中可将 **Code formatting** 设为合并 `main` 前的必过检查。

GitHub Actions 会递归加载 `workflows/` 下任意子目录中的 workflow 文件。
