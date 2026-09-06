<p align="center">
  <img src="app/src/desktop/resources/logo.png" width="250" alt="QTrans">
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

- [Conan 2](https://conan.io/) 2.28+
- CMake 3.31+, Ninja
- macOS: `brew install ninja pkg-config autoconf autoconf-archive automake libtool`
- Windows: Visual Studio 2022 C++ build tools (run from a Developer shell)

### Build

```bash
# macOS ARM64 (Release)
export CONAN_WORKSPACE_ENABLE=will_break_next
conan profile detect --force
conan install app --profile:host conan/profiles/macos-arm64 --profile:build default \
  --lockfile conan/locks/macos-arm64.lock \
  --output-folder build/arm64-osx-release/conan --build missing
cmake -S app --preset arm64-osx-release
cmake --build build/arm64-osx-release

# Windows MSVC x64 (Release, Vulkan GPU, static CRT/dependencies)
set CONAN_WORKSPACE_ENABLE=will_break_next
conan profile detect --force
conan install app --profile:host conan/profiles/windows-x64-static --profile:build default ^
  --lockfile conan/locks/windows-x64-static.lock ^
  --output-folder build/x64-msvc-static-release/conan --build missing
cmake -S app --preset x64-msvc-static-release
cmake --build build/x64-msvc-static-release

```

The supported build targets are macOS ARM64 with Clang and Windows x64 with MSVC. Under MSVC, third-party libraries and the C/C++ runtime are statically linked; normal Windows system DLL imports remain.

## Development

See the [中文说明](docs/README_zh.md) for project usage and development notes.

## Project Layout

- `app/` - main Conan consumer and CMake project
- `app/src/core/` - reusable translation runtime, backends, and remote/local runtime integration
- `app/src/desktop/` - Qt Widgets application, desktop domain logic, and worker-thread glue
- `app/tests/` - application and core unit tests
- `libs/` - repository-owned Conan packages declared explicitly in `conanws.yml`

## License

[GPL-3.0](LICENSE)
