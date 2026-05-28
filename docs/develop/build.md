# 本地构建

## 前置依赖

- [vcpkg](https://vcpkg.io/)，并设置环境变量 `VCPKG_ROOT`
- CMake 3.21+
- Ninja

平台额外工具：

| 平台 | 依赖 |
|------|------|
| macOS | `brew install ninja pkg-config autoconf autoconf-archive automake libtool` |
| Windows | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 等 MinGW 工具链，并加入 `PATH` |

## CMake Preset

配置定义见仓库根目录 `CMakePresets.json`。

| Preset | 用途 |
|--------|------|
| `default` + `debug` | 本地 Debug，生成 `compile_commands.json`（clangd） |
| `arm64-osx-release` | macOS ARM64 Release |
| `x64-mingw-release` | Windows MinGW x64 Release |

构建目录：`build/<preset 名>/`。

## 命令示例

```bash
# macOS ARM64 Release
cmake --preset arm64-osx-release
cmake --build --preset arm64-osx-release
# 产物：build/arm64-osx-release/QTrans

# Windows MinGW x64 Release
cmake --preset x64-mingw-release
cmake --build --preset x64-mingw-release
# 产物：build/x64-mingw-release/QTrans.exe

# 本地 Debug（任意已配置 triplet 的主机）
cmake --preset default
cmake --build --preset debug
```

可通过环境变量 `VCPKG_DEFAULT_TRIPLET` 覆盖 triplet（与 preset 内设置冲突时以 preset 为准）。

## clangd

```bash
cmake --preset default
```

会在 `build/default/` 下生成 `compile_commands.json`，供 clangd 使用。

## 与 CI 的关系

Release 流水线在 GitHub Actions 中执行与 preset 对应的构建（见 [release.md](release.md)）。本地构建用于开发调试；**合并 PR 不要求** 本地 Release 构建通过。
