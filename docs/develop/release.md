# 发版

## 版本号

使用 [语义化版本](https://semver.org/lang/zh-CN/) 的 tag，前缀 `v`：

```text
v0.2.1
```

## 发布步骤

1. 确认 `main` 上待发布功能已合并  
2. 在 `main` 最新提交上打 tag 并推送：

```bash
git checkout main
git pull origin main
git tag v0.2.1
git push origin v0.2.1
```

3. GitHub Actions **Release** workflow 自动运行（见 [ci.md](ci.md)）  
4. 构建完成后在 [Releases](https://github.com/touken928/QTrans/releases) 查看产物

## 产物命名

| 平台 | 文件名示例 |
|------|------------|
| macOS ARM64 | `QTrans-<version>-macos-arm64`（可执行文件） |
| Windows x64 | `QTrans-<version>-mingw-x64.zip`（含 `QTrans.exe` 与 OpenMP 等 DLL） |

## Workflow 结构

| 文件 | 作用 |
|------|------|
| `release.yml` | tag 触发，编排 macOS / Windows 构建并创建 GitHub Release |
| `release-macos.yml` | 可复用：macOS arm64 构建 |
| `release-windows.yml` | 可复用：MinGW x64 构建与打包 |

## 注意事项

- 仅 **tag push** 触发发版，普通 push `main` 不会发版  
- 发版构建耗时较长（vcpkg / 依赖编译），与 PR 上的格式、分支检查无关  
- 发版失败时在 Actions 中查看对应 job 日志

## 本地验证（可选）

发版前可在本地执行 Release preset 构建，命令见 [build.md](build.md)。
