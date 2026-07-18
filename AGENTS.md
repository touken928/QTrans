# AGENTS.md — QTrans

## Core Boundaries
- This repo is C++17/CMake/Ninja with vcpkg; root `CMakeLists.txt` still owns all targets.
- `src/core/` is the reusable translation runtime. Keep `src/core/include/qtrans/core/` free of llama-cpp, curl, ICU, simdutf, spdlog, Qt, and platform API types.
- `src/desktop/` is the Qt Widgets app layer: UI, settings, download, task queue, word selection, hotkeys, logs, and glue.
- `qtrans_desktop` is the desktop support library and links `qtrans_core`; it is not a reusable core library.
- Backend bootstrap lives in `core::BackendEnvironment`; runtime selection lives in `ITranslationRuntimeFactory`; local/remote behavior differences should flow through `RuntimeTraits` and `TranslationProfile`, not ad-hoc type checks.

## Build And Test
- Public release presets require `VCPKG_ROOT` from the environment; MinGW also requires the toolchain executables on `PATH`. macOS: `cmake --preset arm64-osx-release && cmake --build --preset arm64-osx-release`; Windows: `cmake --preset x64-mingw-static-release && cmake --build --preset x64-mingw-static-release`.
- Tests are opt-in: `cmake --preset arm64-osx-release -DQTRANS_BUILD_TESTS=ON`, then build, then `ctest --test-dir build/arm64-osx-release --output-on-failure`.
- Focus tests by label, e.g. `ctest --test-dir build/arm64-osx-release -L dir:core --output-on-failure` or `-L dir:model`.

## Runtime Notes
- `src/core/runtime/local_runtime.*` wraps local llama-cpp inference; macOS uses Metal and Windows x64 uses Vulkan through the vcpkg `llama-cpp` package.
- `src/core/runtime/remote_runtime.*` calls OpenAI/Anthropic-compatible single-turn APIs using curl privately inside `qtrans_core`.
- Local inference should keep `n_gpu_layers = -1` in resolved `TranslatorOptions`; `BackendOptions` is only for backend selection and plugin dir.
- `Translator` should decide chunking/context behavior from `RuntimeTraits`, not by guessing whether a runtime is local or remote.
- Word-selection translation must fail with a clear context-limit error instead of auto-chunking past the local context window.
- Batch translation should submit work through `BatchController` -> `TaskService::submitBatchTranslate()` -> `TaskOrchestrator` with `TaskPriority::Background`; paused or interactive-preempted work should surface as `TaskState::Preempted`, not `Cancelled`.

## Desktop Boundaries
- Model downloads stay in `src/desktop/domain/download/`; do not move download UI/task behavior into `src/core/`.
- Desktop layout worth keeping: `domain/` for non-UI logic (`batch/`, `download/`, `inference/`, `logging/`, `model-adapters/`, `model-catalog/`, `platform/`, `settings/`, `storage/`, `tasks/`), `ui/` for `shared/`, `sidebar/`, `shell/`, `pages/`, and `popup/`, `shared/` for cross-cutting desktop helpers, and `app/` for entry/glue such as `main.cpp`, `task_service.*`, and `batch_controller.*`.
- Qt string conversion belongs in `src/desktop/shared/string_bridge.*`; do not introduce `QString` into core or other app-independent code.
- `TaskService` runs on a worker `QThread`; widget updates must cross via Qt signals/slots, not direct worker-to-UI calls.
- Batch file translation lives in three layers: `src/desktop/domain/batch/` for Qt-free file types, handlers, and durable queue storage; `src/desktop/app/batch_controller.*` for worker-thread orchestration; and `src/desktop/ui/pages/batch/` for the card queue page, file actions, and language picker UI.
- Batch queue persistence and outputs belong under `AppPaths::batch_dir`; keep queue state in `queue.bq`, write translated files under `batch/output/`, and keep batch-specific settings in `AppSettings::batch_*`.

## Storage And Logs
- Use `AppPaths` (`src/desktop/domain/storage/app_paths.h`) for all app data: portable mode uses `<app>/data/`, system mode uses `~/.qtrans/`.
- Do not write logs or data to process cwd.
- Debug AI traces write prompt/response under the app logs dir; Release builds should not create AI trace files.

## Formatting And CI
- Use root `.clang-format` (Google-derived, 4 spaces, pointer right, `SortIncludes: false`). Format only touched C++ files.
- CI formatting checks `src/core` and `src/desktop` for `*.cpp`, `*.h`, `*.mm` with `clang-format-18`; use `uvx clang-format==18.1.0 -i <file>` or `uvx clang-format==18.1.0 --dry-run --Werror <file>` locally.
- Workflow YAML must stay directly under `.github/workflows/*.yml`; nested workflow dirs will not register.
- CI jobs to remember: `Branch naming`, `Code formatting`, `Unit tests`. `release.yml` fans out into `release-macos.yml` and `release-windows.yml`.

## Git Workflow
- `main` is protected. Use branches named `users/<github-login>/<topic>`; CI rejects other names and owner mismatches.
- Do not commit or push directly to `main`; open a PR to `main` and let GitHub merge.
- Do not change `vcpkg.json`, overlays, or Qt modules unless the task explicitly requires dependency changes.
