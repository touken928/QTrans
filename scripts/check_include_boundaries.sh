#!/usr/bin/env bash
# Enforces the folder dependency rules:
#   runtime includes none of the other app directories
#   download does not include translate, batch, popup, or ui
#   translate does not include batch, popup, or ui
#   batch and popup include only translate headers, plus the leaves
#     settings, paths, logging, shared
#   ui does not include qtrans/runtime.h, curl, windows.h, or popup/mac|win
#   curl and windows.h stay out of headers
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
status=0

fail() {
    echo "include boundary violation: $*" >&2
    status=1
}

includes_matching() {
    local file="$1"
    local pattern="$2"
    grep -nE "#[[:space:]]*include[[:space:]]*[<\"]${pattern}" "$file" || true
}

# 1. runtime does not include the other app directories.
while IFS= read -r file; do
    while IFS=: read -r _ line _; do
        fail "$file:$line runtime includes another app directory"
    done < <(includes_matching "$file" "(translate|download|batch|popup|ui|settings|paths|instance)/")
done < <(find "${ROOT}/app/src/runtime" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))

# 2. download does not include translate, batch, popup, or ui.
while IFS= read -r file; do
    while IFS=: read -r _ line _; do
        fail "$file:$line download includes a higher directory"
    done < <(includes_matching "$file" "(translate|batch|popup|ui)/")
done < <(find "${ROOT}/app/src/download" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))

# 3. translate does not include batch, popup, or ui.
while IFS= read -r file; do
    while IFS=: read -r _ line _; do
        fail "$file:$line translate includes a higher directory"
    done < <(includes_matching "$file" "(batch|popup|ui)/")
done < <(find "${ROOT}/app/src/translate" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))

# 4. batch and popup include only translate headers, plus leaves.
for dir in batch popup; do
    while IFS= read -r file; do
        while IFS=: read -r _ line _; do
            fail "$file:$line ${dir} includes a directory it must not"
        done < <(includes_matching "$file" "(download|ui|instance)/")
        if [ "$dir" = "batch" ]; then
            while IFS=: read -r _ line _; do
                fail "$file:$line batch includes popup"
            done < <(includes_matching "$file" "popup/")
        else
            while IFS=: read -r _ line _; do
                fail "$file:$line popup includes batch"
            done < <(includes_matching "$file" "batch/")
        fi
    done < <(find "${ROOT}/app/src/${dir}" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))
done

# 5. No app header outside runtime/internal exposes curl or windows.h.
while IFS= read -r file; do
    case "$file" in
        "${ROOT}/app/src/runtime/internal/"*) continue ;;
    esac
    while IFS=: read -r _ line _; do
        fail "$file:$line header includes curl or windows.h; keep that in a .cpp/.mm"
    done < <(includes_matching "$file" "(curl/|windows\.h)")
done < <(find "${ROOT}/app/src" -type f -name '*.h')

# 6. ui does not include the runtime API, curl, OS headers, or popup OS dirs.
while IFS= read -r file; do
    while IFS=: read -r _ line _; do
        fail "$file:$line ui includes a forbidden header"
    done < <(includes_matching "$file" "(qtrans/runtime\.h|curl/|windows\.h|Carbon/Carbon\.h|ApplicationServices/ApplicationServices\.h|popup/mac/|popup/win/)")
done < <(find "${ROOT}/app/src/ui" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))

# 7. Nothing outside runtime includes runtime internals.
while IFS= read -r file; do
    case "$file" in
        "${ROOT}/app/src/runtime/"*) continue ;;
    esac
    while IFS=: read -r _ line _; do
        fail "$file:$line includes a runtime internal header"
    done < <(grep -nE '#[[:space:]]*include.*runtime/internal|#[[:space:]]*include[[:space:]]*[<"](local_runtime|model_host_test_access|invocation_scheduler|prompt_profiles|runtime_types)\.h[>"]' "$file" || true)
done < <(find "${ROOT}/app/src" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' \))

# 8. The public runtime include variable must not expose runtime/internal.
if awk '
    /set\(QTRANS_RUNTIME_INCLUDE_DIRS/ { capture = 1 }
    capture && /runtime\/internal/ { found = 1 }
    capture && /\)/ { capture = 0 }
    END { exit found ? 0 : 1 }
' "${ROOT}/app/src/sources.cmake"; then
    fail "app/src/sources.cmake lists runtime/internal in QTRANS_RUNTIME_INCLUDE_DIRS"
fi

# 9. translate headers other than the API edge must not include qtrans/runtime.h.
while IFS= read -r file; do
    case "$file" in
        "${ROOT}/app/src/translate/api_chat.h"|"${ROOT}/app/src/translate/openai_protocol.h") continue ;;
    esac
    while IFS=: read -r _ line _; do
        fail "$file:$line includes qtrans/runtime.h; only the API edge may"
    done < <(includes_matching "$file" "qtrans/runtime\.h")
done < <(find "${ROOT}/app/src/translate" -type f -name '*.h')

if [ "$status" -ne 0 ]; then
    exit 1
fi
echo "Include boundaries OK"
