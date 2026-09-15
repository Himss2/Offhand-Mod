#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="${TMPDIR:-/tmp}/levi-offhand-native-attachment-fix-test"

python3 "$repo_root/tests/native_attachment_source_contract.py"

hand_action_policy_test_binary="${TMPDIR:-/tmp}/levi-offhand-hand-action-routing-core-test"

g++ \
    -std=c++20 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$repo_root/src" \
    "$repo_root/tests/hand_action_routing_core_test.cpp" \
    -o "$hand_action_policy_test_binary"

ASAN_OPTIONS=detect_leaks=0 "$hand_action_policy_test_binary"
printf '%s\n' "hand action routing core test passed"

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

python3 "$repo_root/tests/v0267_reference_carriers_contract.py"

routing_test_binary="${TMPDIR:-/tmp}/levi-offhand-v0262-routing-test"

g++ \
    -std=c++20 \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -I"$repo_root/src" \
    "$repo_root/tests/v0262_auto_insert_routing_test.cpp" \
    -o "$routing_test_binary"

ASAN_OPTIONS=detect_leaks=0 "$routing_test_binary"
printf '%s\n' "v0.2.62 auto-insert routing core test passed"

python3 "$repo_root/tests/v0262_native_routing_source_contract.py"

if [[ -n "${LEVI_MCPE_LIBRARY:-}" ]]; then
    python3 \
        "$repo_root/tests/v0262_native_routing_binary_contract.py" \
        "$LEVI_MCPE_LIBRARY"
else
    printf '%s\n' \
        "v0.2.62 native routing binary contract skipped: LEVI_MCPE_LIBRARY unset"
fi
