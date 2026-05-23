#!/usr/bin/env bash
# List PE import DLLs and fail if MinGW runtime DLLs are required at load time.
set -euo pipefail

exe="${1:?usage: $0 <path-to.exe>}"
objdump="${OBJDUMP:-objdump}"

if ! command -v "${objdump}" >/dev/null 2>&1; then
  echo "error: ${objdump} not found" >&2
  exit 1
fi

imports_file="$(mktemp)"
trap 'rm -f "${imports_file}"' EXIT

"${objdump}" -p "${exe}" >"${imports_file}"

mapfile -t dlls < <(grep -E '^\s+DLL Name:' "${imports_file}" | awk '{print $3}' | sort -u)

echo "Imported DLLs (${#dlls[@]}):"
if ((${#dlls[@]} == 0)); then
  echo "  (none)"
else
  printf '  %s\n' "${dlls[@]}"
fi

forbidden_re='^(libstdc\+\+-6|libgcc_s_seh-1|libwinpthread-1|libgomp-1|libmingwex-0)\.dll$'
bad=()
for dll in "${dlls[@]}"; do
  if [[ "${dll}" =~ ${forbidden_re} ]]; then
    bad+=("${dll}")
  fi
done

if ((${#bad[@]} > 0)); then
  echo "error: MinGW runtime DLL imports detected:" >&2
  printf '  %s\n' "${bad[@]}" >&2
  exit 1
fi

echo "ok: no MinGW runtime DLL imports"
