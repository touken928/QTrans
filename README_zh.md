<p align="center">
  <img src="resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>基于 <a href="https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF">Hy-MT1.5</a> 与 llama.cpp 的本地桌面翻译应用：Qt 6 图形界面、本地 CPU 推理、首次使用可下载模型，支持九种语言互译与回译。</strong>
</p>

<p align="center">
  <a href="README.md">English</a>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.21+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.21+"></a>
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6-41CD52.svg?style=for-the-badge&logo=qt" alt="Qt 6"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg?style=for-the-badge" alt="GPL-3.0"></a>
</p>

## 功能

- Qt 6 图形界面：翻译、回译、模型管理
- 本地 CPU 推理；首次使用时可下载模型
- 支持中文、日语、韩语、英语、法语、德语、西班牙语、阿拉伯语、俄语

## 下载

预编译二进制可在 [Releases](https://github.com/touken928/QTrans/releases) 页面获取：

- macOS arm64：`QTrans-<版本>-macos-arm64`
- Windows x64：`QTrans-<版本>-windows-x64.exe`

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

[GPL-3.0](LICENSE)
