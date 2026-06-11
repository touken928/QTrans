# AGENTS.md — QTrans

## What Matters First
- This repo is C++17/CMake/Ninja with vcpkg; root `CMakeLists.txt` still owns all targets.
- `core/` is the app-independent translation runtime library. Public headers live in `core/include/qtrans/core/` and must not expose llama-cpp, curl, ICU, simdutf, spdlog, Qt, or platform API types.
- `apps/desktop/src/` is the Qt Widgets desktop app layer: UI, settings, model download, task queue, word selection, platform hotkeys, logs, and app glue.
- `qtrans_engine` is the desktop support static library and links `qtrans_core`; it is not the reusable core library.

## Build And Test
- Public presets: `cmake --preset arm64-osx-release && cmake --build --preset arm64-osx-release`; Windows: `cmake --preset x64-mingw-release && cmake --build --preset x64-mingw-release`.
- This checkout also has `CMakeUserPresets.json` with local `*-user` presets and absolute `/Users/touken/...` paths; do not copy those commands into portable docs or CI.
- Tests are opt-in: `cmake --preset arm64-osx-release -DQTRANS_BUILD_TESTS=ON`, then build, then `ctest --test-dir build/arm64-osx-release --output-on-failure`.
- Focus tests by label, e.g. `ctest --test-dir build/arm64-osx-release -L dir:core --output-on-failure` or `-L dir:task`.

## Architecture Boundaries
- `core/src/runtime/local_runtime.*` wraps local llama-cpp inference; macOS uses Metal and Windows x64 uses Vulkan through vcpkg llama-cpp.
- `core/src/runtime/remote_runtime.*` calls OpenAI/Anthropic-compatible single-turn translation APIs using curl privately inside `qtrans_core`.
- `Translator` and `ITranslationRuntime` are the shared runtime abstraction; keep local and network model behavior consistent through this path.
- Model downloads stay in `apps/desktop/src/network/`; do not move download UI/task behavior into `core/`.
- Qt string conversion belongs at `apps/desktop/src/app/string_bridge.*`; do not add `QString` to core or lower app-independent code.
- `TaskService` runs on a worker `QThread`; UI changes should cross via Qt signals/slots, not direct worker-to-widget calls.

## GPU And Runtime Gotchas
- `QTRANS_GPU_METAL` is set when `VCPKG_TARGET_TRIPLET` matches `osx`; `QTRANS_MULTI_BACKEND` is set only for `x64-mingw*`.
- Early backend init is required before model resolution: `TaskOrchestrator::initialize_backend()` calls `ITranslationRuntime::initialize_default_backend()` and then refreshes `RuntimeCapabilities`. Do not make this a no-op.
- Local inference should use `n_gpu_layers=-1` from `TranslatorOptions` / resolved model config; `BackendOptions` is for backend selection and plugin path, not GPU layer count.

## Storage, Logs, Text
- App data must go through `AppPaths` (`apps/desktop/src/storage/app_paths.h`): portable mode uses `<app>/data/`, system mode uses `~/.qtrans/`; do not write data/logs to process cwd.
- Debug AI traces write prompt/response under the app logs dir; Release builds should not create AI trace files.
- Core performs UTF-8 validation, ICU sentence splitting, token-budget chunking. Word selection translation must fail with a clear context-limit error instead of auto-chunking.

## Formatting And CI
- Use root `.clang-format` (Google-derived, 4 spaces, pointer right, `SortIncludes: false`). Format only touched C++ files.
- CI formatting checks `core/include`, `core/src`, and `apps/desktop/src` for `*.cpp`, `*.h`, `*.mm` with `clang-format-18`.
- Workflow YAML files must stay directly under `.github/workflows/*.yml`; nested workflow dirs will not register.

## Git Workflow
- `main` is protected. Use branches named `users/<github-login>/<topic>`; CI rejects other names and owner mismatches.
- Do not commit on or push directly to `main`; open a PR to `main` and let GitHub merge.
- Do not update `vcpkg.json`, overlays, or Qt modules unless the task explicitly requires dependency changes.
