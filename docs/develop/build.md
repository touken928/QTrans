# 本地构建

## 前置依赖

- [vcpkg](https://vcpkg.io/)，并设置环境变量 `VCPKG_ROOT`
- CMake 3.31+
- Ninja

主要 vcpkg 依赖（见根目录 `vcpkg.json`）：`llama-cpp`、`qtbase`、`curl`、`icu`、`simdutf`、`spdlog`、`gtest`。

平台额外工具：

| 平台 | 依赖 |
|------|------|
| macOS | `brew install ninja pkg-config autoconf autoconf-archive automake libtool` |
| Windows | Visual Studio 2022 C++ 构建工具；从 Developer PowerShell/Command Prompt 执行 |

## CMake Preset

配置定义见仓库根目录 `CMakePresets.json`。

| Preset | 用途 |
|--------|------|
| `arm64-osx-release` | macOS ARM64 Release（Metal GPU） |
| `x64-msvc-static-release` | Windows MSVC x64 Release（Vulkan、静态 CRT/依赖） |
| `x64-mingw-static-release` | Windows MinGW x64 Release（Vulkan GPU 静态编入） |

构建目录：`build/<preset 名>/`。

## 命令示例

```bash
# macOS ARM64 Release
cmake --preset arm64-osx-release
cmake --build --preset arm64-osx-release
# 产物：build/arm64-osx-release/src/desktop/QTrans

# Windows MSVC x64 Release
cmake --preset x64-msvc-static-release
cmake --build --preset x64-msvc-static-release
# 产物：build/x64-msvc-static-release/src/desktop/QTrans.exe
# Release 包：仅 QTrans.exe；第三方库与 CRT 静态链接，系统 DLL 除外

```

可通过环境变量 `VCPKG_DEFAULT_TRIPLET` 覆盖 triplet（与 preset 内设置冲突时以 preset 为准）。

## 与 CI 的关系

Release 流水线在 GitHub Actions 中执行与 preset 对应的构建（见 [release.md](release.md)）。PR 会在 CI 中执行全量单元测试（见 [ci.md](ci.md)）。本地构建用于开发调试；**合并 PR 不要求** 本地 Release 构建通过。
