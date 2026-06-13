# AGENTS.md — QTrans

## Core Boundaries
- This repo is C++17/CMake/Ninja with vcpkg; root `CMakeLists.txt` still owns all targets.
- `core/` is the reusable translation runtime. Keep `core/include/qtrans/core/` free of llama-cpp, curl, ICU, simdutf, spdlog, Qt, and platform API types.
- `apps/desktop/src/` is the Qt Widgets app layer: UI, settings, download, task queue, word selection, hotkeys, logs, and glue.
- `qtrans_engine` is the desktop support library and links `qtrans_core`; it is not a reusable core library.
- Backend bootstrap lives in `core::BackendEnvironment`; runtime selection lives in `ITranslationRuntimeFactory`; local/remote behavior differences should flow through `RuntimeTraits` and `TranslationProfile`, not ad-hoc type checks.

## Build And Test
- Public release presets: `cmake --preset arm64-osx-release && cmake --build --preset arm64-osx-release`; Windows: `cmake --preset x64-mingw-release && cmake --build --preset x64-mingw-release`.
- `default` + `debug` is the local debug path and generates `build/default/compile_commands.json` for clangd.
- Tests are opt-in: `cmake --preset arm64-osx-release -DQTRANS_BUILD_TESTS=ON`, then build, then `ctest --test-dir build/arm64-osx-release --output-on-failure`.
- Focus tests by label, e.g. `ctest --test-dir build/arm64-osx-release -L dir:core --output-on-failure` or `-L dir:model`.
- `CMakeUserPresets.json` contains machine-local `*-user` presets with absolute `/Users/touken/...` paths; do not copy those commands into docs or CI.

## Runtime Notes
- `core/src/runtime/local_runtime.*` wraps local llama-cpp inference; macOS uses Metal and Windows x64 uses Vulkan through the vcpkg `llama-cpp` package.
- `core/src/runtime/remote_runtime.*` calls OpenAI/Anthropic-compatible single-turn APIs using curl privately inside `qtrans_core`.
- Local inference should keep `n_gpu_layers = -1` in resolved `TranslatorOptions`; `BackendOptions` is only for backend selection and plugin dir.
- `Translator` should decide chunking/context behavior from `RuntimeTraits`, not by guessing whether a runtime is local or remote.
- Word-selection translation must fail with a clear context-limit error instead of auto-chunking past the local context window.

## Desktop Boundaries
- Model downloads stay in `apps/desktop/src/domain/download/`; do not move download UI/task behavior into `core/`.
- Desktop layout worth keeping: `domain/` for non-UI logic (`download/`, `inference/`, `logging/`, `model-adapters/`, `model-catalog/`, `platform/`, `settings/`, `storage/`, `tasks/`), `ui/` for `shared/`, `sidebar/`, `shell/`, `pages/`, and `popup/`, `shared/` for cross-cutting desktop helpers, and `app/` for entry/glue such as `main.cpp` and `task_service.*`.
- Qt string conversion belongs in `apps/desktop/src/shared/string_bridge.*`; do not introduce `QString` into core or other app-independent code.
- `TaskService` runs on a worker `QThread`; widget updates must cross via Qt signals/slots, not direct worker-to-UI calls.

## Storage And Logs
- Use `AppPaths` (`apps/desktop/src/domain/storage/app_paths.h`) for all app data: portable mode uses `<app>/data/`, system mode uses `~/.qtrans/`.
- Do not write logs or data to process cwd.
- Debug AI traces write prompt/response under the app logs dir; Release builds should not create AI trace files.

## Formatting And CI
- Use root `.clang-format` (Google-derived, 4 spaces, pointer right, `SortIncludes: false`). Format only touched C++ files.
- CI formatting checks `core/include`, `core/src`, and `apps/desktop/src` for `*.cpp`, `*.h`, `*.mm` with `clang-format-18`; use `uvx clang-format==18.1.0 -i <file>` or `uvx clang-format==18.1.0 --dry-run --Werror <file>` locally.
- Workflow YAML must stay directly under `.github/workflows/*.yml`; nested workflow dirs will not register.
- CI jobs to remember: `Branch naming`, `Code formatting`, `Unit tests`. `release.yml` fans out into `release-macos.yml` and `release-windows.yml`.

## Git Workflow
- `main` is protected. Use branches named `users/<github-login>/<topic>`; CI rejects other names and owner mismatches.
- Do not commit or push directly to `main`; open a PR to `main` and let GitHub merge.
- Do not change `vcpkg.json`, overlays, or Qt modules unless the task explicitly requires dependency changes.
