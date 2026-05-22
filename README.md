<p align="center">
  <img src="resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>An LLM translator that runs <a href="https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF">Hy-MT1.5</a> locally, downloads the model automatically, and performs inference on the CPU.</strong>
</p>

<p align="center">
  <a href="docs/README_zh.md">中文说明</a>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.21+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.21+"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg?style=for-the-badge" alt="GPL-3.0"></a>
</p>

## Features

- Translate and back-translate
- Built-in model download and management

## Screenshot

<p align="center">
  <img src="docs/assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## Download

Prebuilt binaries are available on the [Releases](https://github.com/touken928/QTrans/releases) page:

- macOS arm64: `QTrans-<version>-macos-arm64`
- Windows x64: `QTrans-<version>-windows-x64.exe`

Download the file for your platform, make it executable on macOS if needed, then run it. On first launch, open **Model**, download the model, and click **Load**.

## Build from Source

Requirements:

- [vcpkg](https://vcpkg.io/) (set `VCPKG_ROOT`)
- CMake 3.21+, Ninja
- macOS: `ninja`, `pkg-config`, `autoconf`, `automake`, `libtool`
- Windows: MSYS2 MinGW64 toolchain

```bash
export VCPKG_ROOT=/path/to/vcpkg

# macOS (Debug)
cmake --preset dev-macos-arm64
cmake --build --preset dev

# Windows MinGW (Debug)
cmake --preset dev-windows-x64-mingw
cmake --build --preset dev
```

Release presets: `macos-arm64-static`, `windows-x64-mingw-static`.

## License

[GPL-3.0](LICENSE)
