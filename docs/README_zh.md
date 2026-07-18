<p align="center">
  <img src="../src/desktop/resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>在本机运行 <a href="https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF">Hy-MT</a> 翻译模型的 LLM 软件，可自动下载权重；GPU 推理后端静态链入（Windows x64 为 Vulkan，macOS ARM64 为 Metal）。</strong>
</p>

<p align="center">
  <a href="../README.md">English</a>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.31+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.31+"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg?style=for-the-badge" alt="GPL-3.0"></a>
</p>

## 功能

- 翻译与回译
- 内置模型下载与管理
- 划词翻译（鼠标悬停 / 剪贴板捕获）

## 截图

<p align="center">
  <img src="assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## 下载

预编译二进制可在 [Releases](https://github.com/touken928/QTrans/releases) 页面获取：

- `QTrans-<版本>-macos-arm64` — macOS ARM64
- `QTrans-<版本>-mingw-x64.zip` — Windows x64（`QTrans.exe` + `libomp.dll`）
下载对应平台的压缩包。Windows 解压后直接运行 `QTrans.exe`；macOS 上如需请先赋予可执行权限，然后运行。首次使用请打开 **Model** 页面下载模型，再点击 **Load**。默认模型为 **Q4**。

## 从源码构建

### 环境要求

- [vcpkg](https://vcpkg.io/)（设置 `VCPKG_ROOT`）
- CMake 3.31+、Ninja
- macOS：`brew install ninja pkg-config autoconf autoconf-archive automake libtool`
- Windows：MinGW 工具链（如 [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)）需要加入 `PATH`

### 构建

```bash
# macOS ARM64（Release）
cmake --preset arm64-osx-release
cmake --build --preset arm64-osx-release
# 产物：build/arm64-osx-release/src/desktop/QTrans

# Windows MinGW x64（Release，Vulkan GPU）
cmake --preset x64-mingw-static-release
cmake --build --preset x64-mingw-static-release
# 产物：build/x64-mingw-static-release/src/desktop/QTrans.exe

```

公开 preset 提供 macOS ARM64 和 Windows MinGW x64 Release 构建，每个 preset 已内置 triplet。

## 开发

参与贡献、分支规范、CI、发版等说明见 **[开发文档](develop/README.md)**。

## 许可证

[GPL-3.0](../LICENSE)
