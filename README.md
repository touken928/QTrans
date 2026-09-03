<p align="center">
  <img src="src/desktop/resources/logo.png" width="250" alt="QTrans">
</p>

<p align="center">
  <strong>An LLM translator for local models with built-in model downloads, GPU inference (Vulkan on Windows x64, Metal on macOS ARM64), word selection translation, batch file translation, and a local OpenAI-compatible API.</strong>
</p>

<p align="center">
  <a href="docs/README_zh.md">中文说明</a>
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/17"><img src="https://img.shields.io/badge/c++-17-blue.svg?style=for-the-badge&logo=c%2B%2B" alt="C++17"></a>
  <a href="https://cmake.org/"><img src="https://img.shields.io/badge/cmake-3.31+-064F8C.svg?style=for-the-badge&logo=cmake" alt="CMake 3.31+"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue.svg?style=for-the-badge" alt="GPL-3.0"></a>
</p>

## Features

- Translate and back-translate
- Built-in model download and management
- Word selection translation: select text in any app, press a global hotkey, and read the translation in a popup
- Batch file translation for `.txt`, `.md`, and `.srt` with queueing, pause/resume, and saved outputs
- Local OpenAI-compatible API (`/v1/models`, `/v1/chat/completions`) to serve the loaded model to other tools

## Screenshot

<p align="center">
  <img src="docs/assets/screenshot.png" width="860" alt="QTrans screenshot">
</p>

## Download

Prebuilt binaries are available on the [Releases](https://github.com/touken928/QTrans/releases) page:

- `QTrans-<version>-macos-arm64` — macOS ARM64
- `QTrans-<version>-windows-x64.zip` — Windows x64 (`QTrans.exe`, MSVC static runtime)
Download the archive for your platform. On Windows, unzip and run `QTrans.exe`. On macOS, make the app executable if needed, then run it. On first launch, open **Model**, download the model, and click **Load**. Default model: **Q4** on all supported platforms. App data is stored under `~/.qtrans/` in system mode; batch queue state persists under `~/.qtrans/batch/`, and translated batch outputs are written to `~/.qtrans/batch/output/`.

## Build from Source

### Prerequisites

- [vcpkg](https://vcpkg.io/) (set `VCPKG_ROOT`)
- CMake 3.31+, Ninja
- macOS: `brew install ninja pkg-config autoconf autoconf-archive automake libtool`
- Windows: Visual Studio 2022 C++ build tools (run from a Developer shell)

### Build

```bash
# macOS ARM64 (Release)
cmake --preset arm64-osx-release
cmake --build --preset arm64-osx-release

# Windows MSVC x64 (Release, Vulkan GPU, static CRT/dependencies)
cmake --preset x64-msvc-static-release
cmake --build --preset x64-msvc-static-release

```

The primary public presets are macOS ARM64 and Windows MSVC x64 Release. The legacy MinGW preset remains available for development. Under MSVC, third-party libraries and the C/C++ runtime are statically linked; normal Windows system DLL imports remain.

## Development

See [docs/develop/](docs/develop/) (Chinese) for workflow, CI, branch protection, and releases.

## Project Layout

- `src/core/` - reusable translation runtime, backends, and remote/local runtime integration
- `src/desktop/domain/` - desktop-side non-UI logic such as download, settings, storage, inference, and batch translation
- `src/desktop/ui/` - Qt Widgets UI including translate, word selection, batch, model, and shell pages
- `src/desktop/app/` - desktop entry and worker-thread glue such as `main.cpp`, `inference_service.*`, `download_service.*`, and `batch_controller.*`
- `tests/core/` - core runtime unit tests

## License

[GPL-3.0](LICENSE)
