#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="${TMPDIR:-/tmp}/levi-offhand-native-attachment-fix-test"

python3 "$repo_root/tests/native_attachment_source_contract.py"

g++ \
    -std=c++20 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -pthread \
    -I"$repo_root/src" \
    "$repo_root/tests/native_attachment_fix_test.cpp" \
    -o "$test_binary"

ASAN_OPTIONS=detect_leaks=0 "$test_binary"

if [[ -n "${LEVI_MCPE_LIBRARY:-}" ]]; then
    python3 \
        "$repo_root/tests/native_attachment_binary_contract.py" \
        "$LEVI_MCPE_LIBRARY"
else
    printf '%s\n' \
        "native attachment binary contract skipped: LEVI_MCPE_LIBRARY unset"
fi

python3 "$repo_root/tests/v02611_fast_init_contract.py"
