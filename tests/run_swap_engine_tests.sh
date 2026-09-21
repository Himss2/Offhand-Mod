#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
swap_test=$(mktemp)
trap 'rm -f "$swap_test"' EXIT
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined -Itests/swap_stubs -Itests/stubs -Isrc tests/swap_engine_test.cpp -o "$swap_test"
result=0
for case_name in pristine hooked bad_body unmapped occupied main_empty off_empty both_empty; do
 "$swap_test" "$case_name" || result=1
done
exit "$result"
