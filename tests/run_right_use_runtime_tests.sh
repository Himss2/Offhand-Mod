#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary=$(mktemp)
trap 'rm -f "$test_binary"' EXIT
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread \
    -fsanitize=undefined -fno-omit-frame-pointer \
    -Itests/stubs -Isrc tests/right_use_runtime_test.cpp \
    src/runtime/ActionHandContext.cpp -ldl -o "$test_binary"
status=0
for case_name in release_off release_main release_stale transaction_unscoped consume_native_off off_count_sync consume_food consume_container consume_last consume_stale consume_main setter_unscoped shears sword main_pass both_pass main_success main_terminal air_empty_main_instant_manual air_empty_main_instant_swap air_empty_main_no_off air_empty_main_instant_pass air_empty_main_long_use_blocked eat_offhand_manual_blocked eat_offhand_swap_blocked air_snapshot air_main_success bow_block_pass main_scope off_terminal air_main_pass missing_snapshot disabled_air_empty_main disabled; do
    "$test_binary" "$case_name" || status=1
done
exit "$status"
