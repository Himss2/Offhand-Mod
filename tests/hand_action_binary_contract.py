#!/usr/bin/env python3

import hashlib
import pathlib
import re
import subprocess
import sys

EXPECTED_SHA256 = "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"
EXPECTED_BUILD_ID = "868e275cb295e9a275bb29d2258edc2f7dc48761"

# RED phase: exact gameplay RVAs are intentionally unresolved until the
# 1.26.45.1 binary analysis below is complete.
REQUIRED_RVAS = {
    "attack_entity": None,
    "start_destroy_block": None,
    "continue_destroy_block": None,
    "destroy_block": None,
    "use_item": None,
    "use_item_on": None,
    "release_using_item": None,
}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: hand_action_binary_contract.py /path/to/libminecraftpe.so", file=sys.stderr)
        return 2

    lib = pathlib.Path(sys.argv[1])
    digest = hashlib.sha256(lib.read_bytes()).hexdigest()
    assert digest == EXPECTED_SHA256, (digest, EXPECTED_SHA256)

    notes = subprocess.check_output(["readelf", "-n", str(lib)], text=True, errors="replace")
    match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", notes)
    assert match, "GNU Build ID not found"
    assert match.group(1).lower() == EXPECTED_BUILD_ID

    unresolved = [name for name, rva in REQUIRED_RVAS.items() if rva is None]
    assert not unresolved, f"unresolved gameplay boundaries: {', '.join(unresolved)}"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
