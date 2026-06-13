<p align="center">
  <img src="apps/desktop/resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>An LLM translator for local and remote models with built-in model downloads, GPU inference for local backends (Vulkan on Windows x64, Metal on macOS ARM64), word selection translation, and batch file translation.</strong>
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
- Batch file translation for `.txt`, `.md`, and `.srt` with queueing, pause/resume, and saved outputs

## Screenshot

<p align="center">
  <img src="docs/assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## Download

Prebuilt binaries are available on the [Releases](https://github.com/touken928/QTrans/releases) page:

- `QTrans-<version>-macos-arm64` — macOS ARM64
- `QTrans-<version>-mingw-x64.zip` — Windows x64 (`QTrans.exe` + `libomp.dll`)
Download the archive for your platform. On Windows, unzip and run `QTrans.exe`. On macOS, make the app executable if needed, then run it. On first launch, open **Model**, download the model, and click **Load**. Default model: **Q4** on all supported platforms. App data is stored under `~/.qtrans/` in system mode; batch queue state persists under `~/.qtrans/batch/`, and translated batch outputs are written to `~/.qtrans/batch/output/`.

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

# Windows MinGW x64 (Release, Vulkan GPU)
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

## Development

See [docs/develop/](docs/develop/) (Chinese) for workflow, CI, branch protection, and releases.

## Project Layout

- `core/` - reusable translation runtime, backends, and remote/local runtime integration
- `apps/desktop/src/domain/` - desktop-side non-UI logic such as download, settings, storage, tasks, and batch translation
- `apps/desktop/src/ui/` - Qt Widgets UI including translate, word selection, batch, model, and shell pages
- `apps/desktop/src/app/` - desktop entry and worker-thread glue such as `main.cpp`, `task_service.*`, and `batch_controller.*`

## License

[GPL-3.0](LICENSE)
