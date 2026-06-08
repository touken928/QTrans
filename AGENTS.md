# AGENTS.md — QTrans

Instructions for AI coding agents working in this repository.

## Project

QTrans is a **C++17 / Qt6** desktop app for **local CPU inference** with Hy-MT (llama.cpp). Features: translate/back-translate, model download, word-select translation (platform hotkeys + popup).

| Item | Detail |
|------|--------|
| Build | CMake 3.21+, Ninja, **vcpkg** (`VCPKG_ROOT`) |
| UI | Qt6 Widgets |
| Inference | llama.cpp via vcpkg `llama-cpp` |
| License | GPL-3.0 |

Human docs: [README.md](README.md), [docs/README_zh.md](docs/README_zh.md).  
Developer docs: **[docs/develop/](docs/develop/)** (workflow, build, CI, release).

## Repository layout

```text
src/
  app/           # Main window, UI pages, task glue, single-instance
  translation/   # Hy-MT, inference engine, languages
  model/         # Catalog, files
  network/       # Model download (libcurl)
  storage/       # Paths, settings
  task/          # Queue, orchestrator (worker thread)
  wordselect/    # Hotkey, clipboard, popup, session; mac/ win/ platform code
.github/workflows/   # CI + release (YAML at root of workflows/, not subdirs)
docs/develop/    # Contributor documentation
resources/       # Qt resources (icons)
```

`qtrans_engine` static library: core logic under `src/` except app shell and wordselect UI wiring. Executable target: `QTrans`.

## Build (local)

```bash
# Debug + compile_commands.json (clangd)
cmake --preset default && cmake --build --preset debug

# Release
cmake --preset arm64-osx-release && cmake --build --preset arm64-osx-release   # macOS arm64
cmake --preset x64-mingw-release && cmake --build --preset x64-mingw-release   # Windows MinGW
```

Presets: `CMakePresets.json`. Do not add dependencies without updating `vcpkg.json`.

## Tests

Unit tests live in `tests/`, mirrored under `src/` directories (`task/`, `storage/`, `network/`, `model/`, `translation/`). One gtest binary per directory, linked to `qtrans_engine`. Framework: **GoogleTest** via vcpkg (`gtest` in `vcpkg.json`).

```bash
cmake --preset arm64-osx-release-user -DQTRANS_BUILD_TESTS=ON
cmake --build --preset arm64-osx-release-user
ctest --test-dir build/arm64-osx-release-user --output-on-failure
ctest --test-dir build/arm64-osx-release-user -L dir:task   # filter by directory
```

Default is `QTRANS_BUILD_TESTS=OFF`. Pure-logic modules (parsers, state machines, INI I/O) are in scope; Qt-widget / platform-API code is not. To make a private static testable, add a `friend struct XxxTestAccess;` in the class header and define the accessor in `tests/<dir>/xxx_test_access.h`.

## Code style

- Format with repo root [`.clang-format`](.clang-format) (Google-based, 4 spaces, pointer right alignment).
- Scope: `src/**/*.{cpp,h,mm}`.
- Before suggesting commits, ensure changed C++ files would pass:

```bash
clang-format -i <files>
```

CI job **Code formatting** runs `clang-format-18` on PRs to `main`.

## Git & PR workflow (required)

`main` is protected: **no direct push**. All changes go through PR.

1. Branch: `users/<github-login>/<topic>` (e.g. `users/touken928/fix-popup`).
2. Commit on that branch only; never commit on `main`.
3. Push branch → open PR to `main`.
4. CI must pass: **Branch naming**, **Code formatting**.
5. Merge on GitHub; remote head branch is auto-deleted. Local: `git fetch --prune`.

Do **not** run `git push origin main` or `git commit` on `main`.

Details: [docs/develop/workflow.md](docs/develop/workflow.md).

## CI & releases

| Workflow | When |
|----------|------|
| `branch-policy.yml` | push non-`main`, PR → `main` |
| `format-check.yml` | PR → `main` |
| `release.yml` | tag `v*` only |

Release: `git tag vX.Y.Z && git push origin vX.Y.Z` from `main` (maintainer).

Workflow files **must** live in `.github/workflows/*.yml` (not nested subfolders).

## Agent guidelines

1. **Minimal diffs** — Only change what the task needs; no unrelated refactors or drive-by formatting outside touched files.
2. **Match existing patterns** — Naming, Qt parent/ownership, signals/slots, include order, platform `#if` in `wordselect/mac|win/`.
3. **Threading** — `TaskService` runs on a worker `QThread`; UI updates via signals to `MainWindow` / pages.
4. **Platform code** — macOS: `src/app/mac/`, `src/wordselect/mac/`. Windows: `src/app/win/`, `src/wordselect/win/`. Keep shared logic platform-agnostic in `src/`.
5. **No secrets** — Never commit API keys, tokens, or local paths in settings.
6. **No vcpkg churn** — Avoid bumping `vcpkg.json` / overlays unless explicitly requested.
7. **Commits** — Only when the user asks; follow repo commit style (`feat:`, `fix:`, `ci:`, etc.).
8. **Docs** — Contributor-facing changes: update relevant file under `docs/develop/` if behavior or workflow changes.

## Common pitfalls

- Adding `Qt6::Network` or extra Qt modules without manifest update in `vcpkg.json`.
- Putting GitHub workflows under `.github/workflows/ci/` (GitHub may not register them).
- Using branch names outside `users/<login>/...` (CI fails).
- Editing only `main` locally then expecting push to work (Ruleset blocks).

## Quick links

- [Workflow](docs/develop/workflow.md)
- [Build](docs/develop/build.md)
- [Code style](docs/develop/code-style.md)
- [CI](docs/develop/ci.md)
- [Branch protection](docs/develop/branch-protection.md)
- [Release](docs/develop/release.md)
