<p align="center">
  <img src="resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>An LLM translator that runs <a href="https://huggingface.co/AngelSlim/Hy-MT2-1.8B-1.25Bit-GGUF">Hy-MT</a> models locally, downloads weights automatically, and performs inference on the CPU.</strong>
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
- Word selection translation (hover or clipboard capture)

## Screenshot

<p align="center">
  <img src="docs/assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## Download

Prebuilt binaries are available on the [Releases](https://github.com/touken928/QTrans/releases) page:

- `QTrans-<version>-macos-arm64` — macOS ARM64
- `QTrans-<version>-mingw-x64.zip` — Windows x64 (contains `QTrans.exe` and OpenMP runtime DLLs)

Download the archive for your platform. On Windows, unzip and run `QTrans.exe`. On macOS, make the app executable if needed, then run it. On first launch, open **Model**, download the model, and click **Load**.

## Build from Source

### Prerequisites

- [vcpkg](https://vcpkg.io/) (set `VCPKG_ROOT`)
- CMake 3.21+, Ninja
- macOS: `brew install ninja pkg-config autoconf autoconf-archive automake libtool`
- Windows: MinGW toolchain (e.g. [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)) in `PATH`

### Build

```bash
# macOS ARM64 (Release)
cmake --preset arm64-osx-release
cmake --build --preset arm64-osx-release

# Windows MinGW x64 (Release)
cmake --preset x64-mingw-release
cmake --build --preset x64-mingw-release

# Debug (any platform, using VCPKG_DEFAULT_TRIPLET)
cmake --preset default
cmake --build --preset debug
```

The triplet is set per preset. Override with `VCPKG_DEFAULT_TRIPLET` env var if needed.

### clangd

Configure with the `default` preset to generate `compile_commands.json` for clangd:

```bash
cmake --preset default
```

## License

[GPL-3.0](LICENSE)
