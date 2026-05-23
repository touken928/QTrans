<p align="center">
  <img src="../resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>在本机运行 <a href="https://huggingface.co/AngelSlim/Hy-MT2-1.8B-1.25Bit-GGUF">Hy-MT</a> 翻译模型的 LLM 软件，可自动下载权重并在 CPU 上完成推理。</strong>
</p>

<p align="center">
  <a href="../README.md">English</a>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.21+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.21+"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg?style=for-the-badge" alt="GPL-3.0"></a>
</p>

## 功能

- 翻译与回译
- 内置模型下载与管理

## 截图

<p align="center">
  <img src="assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## 下载

预编译二进制可在 [Releases](https://github.com/touken928/QTrans/releases) 页面获取：

- macOS arm64：`QTrans-<版本>-macos-arm64`

Windows 发布构建已暂时从 CI 中移除；如需可自行用 `windows-x64-mingw-static` 预设本地编译。

下载对应平台的文件，在 macOS 上如需请先赋予可执行权限，然后运行。首次使用请打开 **Model** 页面下载模型，再点击 **Load**。

## 从源码构建

环境要求：

- [vcpkg](https://vcpkg.io/)（设置 `VCPKG_ROOT`）
- CMake 3.21+、Ninja
- macOS：`ninja`、`pkg-config`、`autoconf`、`automake`、`libtool`
- Windows：MSYS2 MinGW64 工具链

```bash
export VCPKG_ROOT=/path/to/vcpkg

# macOS（Debug）
cmake --preset dev-macos-arm64
cmake --build --preset dev

# Windows MinGW（Debug）
cmake --preset dev-windows-x64-mingw
cmake --build --preset dev
```

Release 预设：`macos-arm64-static`、`windows-x64-mingw-static`。

## 许可证

[GPL-3.0](../LICENSE)
