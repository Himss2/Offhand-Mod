#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
swap_test=$(mktemp)
trap 'rm -f "$swap_test"' EXIT
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -Itests/swap_stubs -Itests/stubs -Isrc tests/swap_engine_test.cpp src/runtime/ActionHandContext.cpp -o "$swap_test"
result=0
for case_name in pristine bad_body unmapped occupied main_empty off_empty both_empty identical repeat_same_item active_use stop_callback invalid_flag invalid_index missing_state rejected_write; do
 "$swap_test" "$case_name" || result=1
done
exit "$result"
