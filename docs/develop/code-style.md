# 代码风格

## 工具

使用 [clang-format](https://clang.org/docs/ClangFormat.html)，配置文件为仓库根目录 [`.clang-format`](../../.clang-format)（基于 Google 风格，4 空格缩进，指针右对齐等）。

## 格式化范围

`src/` 下所有 `.cpp`、`.h`、`.mm` 文件。

## 本地格式化

单文件：

```bash
clang-format -i path/to/file.cpp
```

全部源码：

```bash
while IFS= read -r -d '' f; do clang-format -i "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

## 提交前检查（不自动改文件）

```bash
while IFS= read -r -d '' f; do clang-format --dry-run --Werror "$f"; done \
  < <(find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0)
```

## CI

Pull Request  targeting `main` 时，GitHub Actions 运行 **Code formatting**（`clang-format-18`）。未通过则 PR 不应合并（若 Ruleset 已启用必选检查）。详见 [ci.md](ci.md)。

## 版本差异

CI 使用 **clang-format-18**，本地可使用较新版本。若仅 CI 失败，请以 CI 日志为准调整，或在本地安装 18 复现。
