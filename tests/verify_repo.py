#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
RP = ROOT / "resources" / "minecraft_resource_packs" / "BetterInventory"

errors: list[str] = []

def require(cond: bool, msg: str) -> None:
    if not cond:
        errors.append(msg)


def load_json(path: Path):
    require(path.is_file(), f"missing JSON file: {path.relative_to(ROOT)}")
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        errors.append(f"invalid strict JSON {path.relative_to(ROOT)}: {exc}")
        return None

manifest = load_json(ROOT / "manifest.json")
if isinstance(manifest, dict):
    require(manifest.get("type") == "preload-native", "manifest type must be preload-native")
    require(manifest.get("entry") == "libbetter_inventory.so", "manifest entry must be libbetter_inventory.so")
    require(manifest.get("minecraft_versions") == ["1.26.45*"], "manifest minecraft_versions must be ['1.26.45*']")
    require(manifest.get("version") == "0.2.0", "manifest version must be 0.2.0")

rp_manifest = load_json(RP / "manifest.json")
if isinstance(rp_manifest, dict):
    header = rp_manifest.get("header", {})
    modules = rp_manifest.get("modules", [])
    require(header.get("name") == "Better Inventory UI", "resource-pack header name mismatch")
    require(header.get("version") == [0, 2, 0], "resource-pack version must be [0, 2, 0]")
    require(any(isinstance(m, dict) and m.get("type") == "resources" for m in modules), "resource pack needs a resources module")

ui_defs = load_json(RP / "ui" / "_ui_defs.json")
if isinstance(ui_defs, dict):
    defs = ui_defs.get("ui_defs", [])
    require("ui/chest_screen.json" in defs, "_ui_defs must include chest_screen.json")
    require("ui/pocket_containers.json" in defs, "_ui_defs must include pocket_containers.json")

classic = load_json(RP / "ui" / "chest_screen.json")
pocket = load_json(RP / "ui" / "pocket_containers.json")
for name, doc in (("classic", classic), ("pocket", pocket)):
    if isinstance(doc, dict):
        text = json.dumps(doc, sort_keys=True)
        require("button.better_inventory_sort_small" in text, f"{name} UI lacks small chest sort event")
        require("button.better_inventory_sort_large" in text, f"{name} UI lacks large chest sort event")

for lang in ("en_US.lang", "id_ID.lang", "languages.json"):
    require((RP / "texts" / lang).is_file(), f"missing resource-pack text file: {lang}")


# Standalone Levi native mods must link the public preloader target. Leaving
# SDK symbols unresolved creates a library that Android can compile but cannot
# dlopen (`ll::mod::NativeMod::current()` is the first missing symbol).
cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
require('BETTER_INVENTORY_LINK_PRELOADER' not in cmake_text, "preloader linking must not be optional")
require('--unresolved-symbols=ignore-all' not in cmake_text, "CMake must not allow unresolved preloader SDK symbols")
require('FetchContent_MakeAvailable(preloader_android)' in cmake_text, "CMake must materialize preloader-android")
require(
    'better_inventory_core\n        preloader)' in cmake_text,
    "native mod must link the preloader target",
)
for build_file in (ROOT / "scripts" / "build-android.sh", ROOT / "scripts" / "build-android.ps1", ROOT / ".github" / "workflows" / "build.yml"):
    text = build_file.read_text(encoding="utf-8")
    require('BETTER_INVENTORY_LINK_PRELOADER' not in text, f"{build_file.relative_to(ROOT)} must not expose a preloader-off switch")
workflow_text = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
require('libpreloader.so' in workflow_text, "Android CI must verify the libpreloader.so DT_NEEDED dependency")

for required in (
    ROOT / "scripts" / "package_levipack.py",
    ROOT / "scripts" / "build-android.sh",
    ROOT / "scripts" / "build-android.ps1",
    ROOT / ".github" / "workflows" / "build.yml",
    ROOT / "README.md",
    ROOT / "docs" / "reverse-engineering" / "1.26.45.1.md",
):
    require(required.is_file(), f"missing repository file: {required.relative_to(ROOT)}")

# Source repo must never accidentally contain packaged shared libraries.
so_files = [p for p in ROOT.rglob("*.so") if "build" not in p.parts and "dist" not in p.parts]
require(not so_files, "source tree must not contain committed .so files")


# v0.2 runtime integration invariants. These intentionally fail until the
# Mod Menu + runtime gating implementation is present.
mod_cpp = (ROOT / "src" / "BetterInventoryMod.cpp").read_text(encoding="utf-8")
runtime_hpp = (ROOT / "src" / "runtime" / "ChestRuntime.hpp").read_text(encoding="utf-8")
runtime_cpp = (ROOT / "src" / "runtime" / "ChestRuntime.cpp").read_text(encoding="utf-8")
require('#include <pl/ModMenu.hpp>' in mod_cpp, "native mod must use the official Mod Menu API")
require('better_inventory.chest_sorting' in mod_cpp, "Better Inventory Mod Menu module id is missing")
require('ModuleBuilder' in mod_cpp and '.registerModule()' in mod_cpp, "Better Inventory must register a Mod Menu module")
require('unregisterModule' in mod_cpp, "Better Inventory must unregister its Mod Menu module")
require('setSortingEnabled' in runtime_hpp, "ChestRuntime needs a sorting enable gate")
require('sortingEnabled' in runtime_hpp, "ChestRuntime needs a readable sorting state for tests")
require('isInteracted' in runtime_cpp, "Sort event must gate on the final interacted event")
require('Sort pressed' in runtime_cpp and 'Sort plan' in runtime_cpp, "Sort runtime diagnostics are missing")

if errors:
    for err in errors:
        print(f"ERROR: {err}", file=sys.stderr)
    raise SystemExit(1)
print("Better Inventory repository verification passed")
