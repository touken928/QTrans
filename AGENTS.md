# AGENTS.md — QTrans

## Core Boundaries
- This repo is a Conan workspace. `app/` is the main C++17/CMake/Ninja consumer and owns the top-level `CMakeLists.txt`; the repository root is not a CMake project.
- `libs/*/conanfile.py` is reserved for repository-owned Conan packages. Every workspace member must be declared explicitly in root `conanws.yml`.
- `app/src/core/` is the reusable translation runtime. `app/src/core/include/qtrans/core.h` is the only public core header; llama-cpp, curl, ICU, simdutf, spdlog, Qt, and platform API types belong only in `app/src/core/internal/` implementation headers.
- `app/src/desktop/` is the Qt Widgets app layer: UI, settings, download, inference, word selection, hotkeys, logs, and glue.
- `qtrans_desktop` is the desktop support library and links `qtrans_core`; it is not a reusable core library.
- Backend bootstrap and selection are exposed only through `qtrans/core.h`; local llama-cpp runtime, backend probing, chunking, and callback control are `app/src/core/internal/` implementation details.

## Build And Test
- Run `conan install` with the matching repository profile before a public release preset. The Conan build context must use C++17. Supported targets are macOS ARM64 with Clang and Windows x64 with MSVC only. The exact commands are documented in `README.md`.
- Tests are opt-in: from `app/`, run `cmake --preset arm64-osx-release -DQTRANS_BUILD_TESTS=ON`, then build, then run `ctest --test-dir ../build/arm64-osx-release --output-on-failure`.
- Focus tests by label, e.g. `ctest --test-dir build/arm64-osx-release -L dir:core --output-on-failure` or `-L dir:model`.

## Runtime Notes
- `app/src/core/internal/local_runtime.*` wraps local llama-cpp inference; macOS uses Metal and Windows x64 uses Vulkan through the Conan `llama-cpp` package.
- Local inference chooses CPU/GPU layer behavior inside the internal LocalRuntime from the resolved backend.
- Desktop inference goes exclusively through `ModelHost`, owned by `InferenceService` (`app/src/desktop/app/inference_service.*`); chunking and context budgeting are internal core/host implementation details behind the public `ModelHost` boundary.
- Word-selection translation must fail with a clear context-limit error instead of auto-chunking past the local context window.
- Batch translation should submit work through `BatchController` -> `InferenceService::translateBatch()` (core `WorkClass::Batch`); paused or interactive-preempted work should surface as `TranslationState::Preempted`, not `Cancelled`.
- `LocalApiService` (`app/src/desktop/app/local_api_service.*`) is the app's only network server: it serves the loaded local model as a loopback-only OpenAI-compatible HTTP API (`/v1/models`, `/v1/chat/completions`) bridged through `InferenceService`. There is no remote-model inference; enable/port come from `AppSettings` (`api_enabled`/`api_port`, default 8000) via Preferences → Integrations.

## Desktop Boundaries
- Model downloads stay in `app/src/desktop/domain/download/`; do not move download UI/task behavior into `app/src/core/`.
- Desktop layout worth keeping: `domain/` for non-UI logic (`batch/`, `download/`, `inference/`, `logging/`, `model-catalog/`, `platform/`, `settings/`, `storage/`), `ui/` for `shared/`, `sidebar/`, `shell/`, `pages/`, and `popup/`, `shared/` for cross-cutting desktop helpers, and `app/` for entry/glue such as `main.cpp`, `inference_service.*`, `download_service.*`, `batch_controller.*`, and `local_api_service.*`.
- Qt string conversion belongs in `app/src/desktop/shared/string_bridge.*`; do not introduce `QString` into core or other app-independent code.
- `InferenceService` owns the desktop `ModelHost`; `DownloadService` owns dedicated download execution. Both run on the worker `QThread`; widget updates must cross via Qt signals/slots, not direct worker-to-UI calls. Download cancellation uses `DownloadCancelToken` (`app/src/desktop/domain/download/download_cancellation.h`).
- Word-selection translation is a global-hotkey + popup flow, not a hover/copy detector: `app/src/desktop/domain/platform/clipboard/` and `app/src/desktop/domain/platform/hotkeys/` capture the selection when the user presses the shortcut, and `app/src/desktop/ui/popup/session_controller.*` drives the popup window. Keep that flow out of core and out of `ui/pages/`.
- Batch file translation lives in three layers: `app/src/desktop/domain/batch/` for Qt-free file types, handlers, and durable queue storage; `app/src/desktop/app/batch_controller.*` for worker-thread orchestration; and `app/src/desktop/ui/pages/batch/` for the card queue page, file actions, and language picker UI.
- Batch queue persistence and outputs belong under `AppPaths::batch_dir`; keep queue state in `queue.bq`, write translated files under `batch/output/`, and keep batch-specific settings in `AppSettings::batch_*`.

## Storage And Logs
- Use `AppPaths` (`app/src/desktop/domain/storage/app_paths.h`) for all app data: portable mode uses `<app>/data/`, system mode uses `~/.qtrans/`.
- Do not write logs or data to process cwd.
- Debug AI traces write prompt/response under the app logs dir; Release builds should not create AI trace files.

## Formatting And CI
- Use root `.clang-format` (Google-derived, 4 spaces, pointer right, `SortIncludes: false`). Format only touched C++ files.
- CI formatting checks `app/src/core` and `app/src/desktop` for `*.cpp`, `*.h`, `*.mm` with `clang-format-18`; use `uvx clang-format==18.1.0 -i <file>` or `uvx clang-format==18.1.0 --dry-run --Werror <file>` locally.
- Workflow YAML must stay directly under `.github/workflows/*.yml`; nested workflow dirs will not register.
- CI jobs to remember: `Branch naming`, `Code formatting`, `Unit tests`. `release.yml` fans out into `release-macos.yml` and `release-windows.yml`.

## Git Workflow
- `main` is protected. Use branches named `users/<github-login>/<topic>`; CI rejects other names and owner mismatches.
- Do not commit or push directly to `main`; open a PR to `main` and let GitHub merge.
- Do not change `app/conanfile.py`, Conan profiles, or Qt modules unless the task explicitly requires dependency changes.
