# AGENTS.md — QTrans

## Source Boundaries
- This repo is a Conan workspace. `app/` is the main C++17/CMake/Ninja consumer and owns the top-level `CMakeLists.txt`; the repository root is not a CMake project.
- `libs/*/conanfile.py` is reserved for repository-owned Conan packages. Every workspace member must be declared explicitly in root `conanws.yml`.
- The application has one formal build target, `QTrans`. Do not add internal static libraries. Tests compile the shared source lists in `app/src/sources.cmake` directly.
- `app/src/runtime/` runs the model. Its public headers are only `qtrans/runtime.h`, `qtrans/backend.h`, and `qtrans/lifecycle.h`. llama-cpp, simdutf, spdlog, and platform API types belong only in `app/src/runtime/internal/`. The C++ namespace remains `qtrans::core`. `runtime/` includes none of the other app directories.
- The other source directories are named for what they are, not for a layer role: `translate/` for one translation (`InferenceService`, job types, `ApiChatBridge`, `LocalApiService`); `download/` for model files (catalog, request, curl, checksum, which backend); `batch/` for the file queue; `popup/` for word selection (hotkeys, clipboard, popup session); `ui/` for windows (`MainWindow`, `ModelFlow`, pages). `settings/`, `paths/`, `logging/`, and `shared/` are leaves anyone may include. `worker_host.*` sits next to `main.cpp`. `instance/` is single-instance process startup, not a layer.
- Dependency: `download/` does not include `translate/`, `batch/`, `popup/`, or `ui/`. `translate/` may include `download/` catalog types and must not include `batch/`, `popup/`, or `ui/`. `batch/` and `popup/` include only `translate/` headers, plus the leaves. `ui/` may include those headers and must not include `qtrans/runtime.h`, curl, or `windows.h`. Curl and `windows.h` stay in `.cpp`/`.mm`.

## Build And Test
- Run `conan install` with the matching repository profile before a public release preset. The Conan build context must use C++17. Supported targets are macOS ARM64 with Clang and Windows x64 with clang-cl, lld-link, and the MSVC ABI only. The exact commands are documented in `README.md`.
- Tests are opt-in: from `app/`, run `cmake --preset arm64-osx-release -DQTRANS_BUILD_TESTS=ON`, then build, then run `ctest --test-dir ../build/arm64-osx-release --output-on-failure`.
- Focus tests by label, e.g. `ctest --test-dir build/arm64-osx-release -L dir:runtime --output-on-failure` or `-L dir:model`.

## Runtime Notes
- `app/src/runtime/internal/local_runtime.*` wraps local llama-cpp inference; macOS uses Metal and Windows x64 uses Vulkan through the Conan `llama-cpp` package.
- Local inference chooses CPU/GPU layer behavior inside the internal LocalRuntime from the resolved backend.
- Application inference goes exclusively through `ModelHost`, owned by `InferenceService` (`app/src/translate/inference_service.*`); chunking and context budgeting are internal runtime/host implementation details behind the `ModelHost` boundary.
- Word-selection translation must fail with a clear context-limit error instead of auto-chunking past the local context window.
- Batch translation should submit work through `BatchController` -> `InferenceService::translateBatch()` (`WorkClass::Batch`); paused or interactive-preempted work should surface as `TranslationState::Preempted`, not `Cancelled`.
- `LocalApiService` (`app/src/translate/local_api_service.*`) is the app's only network server: it serves the loaded local model as a loopback-only OpenAI-compatible HTTP API (`/v1/models`, `/v1/chat/completions`) bridged through `ApiChatBridge`. There is no remote-model inference; enable/port come from `AppSettings` (`api_enabled`/`api_port`, default 8000) via Preferences → Integrations.

## Application Boundaries
- Model downloads stay in `app/src/download/`; do not move download UI into `app/src/runtime/`.
- Do not reintroduce `domain/`, `application/`, or `platform/` as source directories.
- Qt string conversion belongs in `app/src/shared/string_bridge.*`; do not introduce `QString` into `runtime/`.
- `InferenceService` owns `ModelHost`; `DownloadService` owns dedicated download execution. Both run on the worker `QThread`, parented by `WorkerHost`. Widget updates must cross via Qt signals/slots, not direct worker-to-UI calls. Download cancellation uses `DownloadCancelToken` (`app/src/download/download_cancellation.h`).
- Word-selection translation is a global-hotkey + popup flow, not a hover/copy detector: `app/src/popup/` captures the selection and drives the popup session. Keep that flow out of `runtime/` and out of `ui/pages/`.
- Batch file translation lives in `app/src/batch/` (types, `queue.bq`, `BatchController`) and `app/src/ui/pages/batch/` (the queue page).
- Batch queue persistence and outputs belong under `AppPaths::batch_dir`; keep queue state in `queue.bq`, write translated files under `batch/output/`, and keep batch-specific settings in `AppSettings::batch_*`.

## Storage And Logs
- Use `AppPaths` (`app/src/paths/app_paths.h`) for all app data: portable mode uses `<app>/data/`, system mode uses `~/.qtrans/`.
- Do not write logs or data to process cwd.
- Debug AI traces write prompt/response under the app logs dir; Release builds should not create AI trace files.

## Formatting And CI
- Use root `.clang-format` (Google-derived, 4 spaces, pointer right, `SortIncludes: false`). Format only touched C++ files.
- CI formatting checks `app/src` for `*.cpp`, `*.h`, `*.mm` with `clang-format-18`; use `uvx clang-format==18.1.0 -i <file>` or `uvx clang-format==18.1.0 --dry-run --Werror <file>` locally.
- Workflow YAML must stay directly under `.github/workflows/*.yml`; nested workflow dirs will not register.
- CI jobs to remember: `Branch naming`, `Code formatting`, `Unit tests`. `release.yml` fans out into `release-macos.yml` and `release-windows.yml`.

## Git Workflow
- `main` is protected. Use branches named `users/<github-login>/<topic>`; CI rejects other names and owner mismatches.
- Do not commit or push directly to `main`; open a PR to `main` and let GitHub merge.
- Do not change `app/conanfile.py`, Conan profiles, or Qt modules unless the task explicitly requires dependency changes.
