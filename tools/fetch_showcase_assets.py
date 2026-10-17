#!/usr/bin/env python3
"""Prepare the open-data asset area for showcase_demo.

The script is deliberately conservative. By default it creates the expected
directory structure and attribution manifest without touching the network. Use
`--download` only when the execution environment has network access and the
operator wants to populate optional source caches.
"""

from __future__ import annotations

import argparse
import json
import shutil
import zipfile
from datetime import date
from pathlib import Path
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = ROOT / "assets" / "showcase"
SOURCE = ASSET_ROOT / "source"
CACHE = ASSET_ROOT / "cache"
RUNTIME = ASSET_ROOT / "runtime"
ATTRIBUTION = ASSET_ROOT / "SOURCE_ATTRIBUTION.md"


OPEN_SOURCES = [
    (
        "AWS Terrain Tiles",
        "Terrarium PNG DEM tiles",
        "https://registry.opendata.aws/terrain-tiles/",
    ),
    (
        "EOX Sentinel-2 cloudless",
        "Web Mercator imagery tiles",
        "https://s2maps.eu/ and https://tiles.maps.eox.at/; license summary: https://cloudless.eox.at/documentation/license",
    ),
    (
        "OpenStreetMap",
        "ODbL vector map data",
        "https://www.openstreetmap.org/copyright",
    ),
    (
        "Overture Maps",
        "Open buildings/transport/base map layers",
        "https://docs.overturemaps.org/getting-data/",
    ),
    (
        "Poly Haven",
        "CC0 PBR materials and HDRIs",
        "https://polyhaven.com/license",
    ),
    (
        "Kenney",
        "CC0 game-ready props",
        "https://kenney.nl/support",
    ),
    (
        "Khronos glTF Sample Models",
        "glTF validation and stress assets",
        "https://github.com/KhronosGroup/glTF-Sample-Models",
    ),
]

DOWNLOADS = [
    {
        "name": "Khronos Box.glb",
        "url": "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/main/2.0/Box/glTF-Binary/Box.glb",
        "source": SOURCE / "khronos" / "Box.glb",
        "runtime": RUNTIME / "khronos_box.glb",
    },
    {
        "name": "Khronos CesiumMilkTruck.glb",
        "url": "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/main/2.0/CesiumMilkTruck/glTF-Binary/CesiumMilkTruck.glb",
        "source": SOURCE / "khronos" / "CesiumMilkTruck.glb",
        "runtime": RUNTIME / "khronos_cesium_milk_truck.glb",
    },
]

KENNEY_FACTORY_KIT = {
    "name": "Kenney Factory Kit 3.0",
    "url": "https://kenney.nl/media/pages/assets/factory-kit/edaac9d4f6-1777639602/kenney_factory-kit_3.0.zip",
    "source": SOURCE / "kenney" / "kenney_factory-kit_3.0.zip",
    "extract_dir": SOURCE / "kenney" / "factory_kit",
}

KENNEY_RUNTIME_ASSETS = {
    "conveyor-long.glb": "kenney_conveyor_long.glb",
    "conveyor-corner.glb": "kenney_conveyor_corner.glb",
    "machine.glb": "kenney_machine.glb",
    "robot-arm-a.glb": "kenney_robot_arm_a.glb",
    "pipe-large-long.glb": "kenney_pipe_large_long.glb",
    "pipe-large-bend.glb": "kenney_pipe_large_bend.glb",
    "crane.glb": "kenney_crane.glb",
    "screen-panel-wide.glb": "kenney_screen_panel_wide.glb",
    "structure-yellow-tall.glb": "kenney_structure_yellow_tall.glb",
    "warning-traffic.glb": "kenney_warning_traffic.glb",
    "box-large.glb": "kenney_box_large.glb",
    "box-small.glb": "kenney_box_small.glb",
    "catwalk-straight.glb": "kenney_catwalk_straight.glb",
    "catwalk-stairs.glb": "kenney_catwalk_stairs.glb",
    "hopper-square.glb": "kenney_hopper_square.glb",
    "machine-window.glb": "kenney_machine_window.glb",
    "pipe-large-valve.glb": "kenney_pipe_large_valve.glb",
    "pipe-large-junction.glb": "kenney_pipe_large_junction.glb",
    "crane-lift.glb": "kenney_crane_lift.glb",
    "warning-orange.glb": "kenney_warning_orange.glb",
}

POLYHAVEN_MATERIALS = [
    {
        "role": "terrain",
        "asset": "aerial_grass_rock",
        "runtime": "terrain_diffuse.jpg",
    },
    {
        "role": "asphalt",
        "asset": "asphalt_02",
        "runtime": "asphalt_diffuse.jpg",
    },
    {
        "role": "concrete",
        "asset": "concrete_floor_02",
        "runtime": "concrete_diffuse.jpg",
    },
    {
        "role": "metal",
        "asset": "metal_plate_02",
        "runtime": "metal_diffuse.jpg",
    },
]


def ensure_dirs() -> None:
    for path in (SOURCE, CACHE, RUNTIME):
        path.mkdir(parents=True, exist_ok=True)


def write_manifest() -> None:
    lines = [
        "# Xineng Benyuan Showcase Assets",
        "",
        f"Last prepared: {date.today().isoformat()}",
        "",
        "This manifest records the open sources allowed for showcase_demo.",
        "Downloaded files, when present, live under `assets/showcase/source/`.",
        "",
        "## Allowed Sources",
        "",
    ]
    for name, purpose, url in OPEN_SOURCES:
        lines.append(f"- {name}: {purpose}. {url}")
    lines.extend(["", "## Downloaded Runtime Candidates", ""])
    for item in DOWNLOADS:
        lines.append(f"- {item['name']}: {item['url']}")
    lines.append(f"- {KENNEY_FACTORY_KIT['name']}: {KENNEY_FACTORY_KIT['url']}")
    for item in POLYHAVEN_MATERIALS:
        lines.append(
            f"- Poly Haven {item['asset']} diffuse 1K ({item['role']} material): "
            f"https://polyhaven.com/a/{item['asset']}"
        )
    lines.extend(
        [
            "",
            "## Local Fallbacks",
            "",
            "- Procedural industrial scene generated by `apps/showcase_demo`.",
            "- `assets/showcase/runtime/industrial_equipment.gltf`: tiny procedural CC0-style equipment crate used to exercise the runtime glTF loading path.",
            "- Existing `assets/badaling/badaling_terrain.bin` may be used only as a terrain-loading smoke fixture.",
            "",
        ]
    )
    ATTRIBUTION.write_text("\n".join(lines), encoding="utf-8")


def download_text(url: str, out: Path) -> None:
    req = Request(url, headers={"User-Agent": "HongTu showcase asset fetcher"})
    with urlopen(req, timeout=60) as response:
        out.write_bytes(response.read())


def download_binary(url: str, out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    req = Request(url, headers={"User-Agent": "HongTu showcase asset fetcher"})
    with urlopen(req, timeout=90) as response:
        out.write_bytes(response.read())


def prepare_kenney_runtime() -> None:
    archive = KENNEY_FACTORY_KIT["source"]
    extract_dir = KENNEY_FACTORY_KIT["extract_dir"]
    if not archive.exists():
        return
    if not extract_dir.exists():
        with zipfile.ZipFile(archive) as zip_file:
            zip_file.extractall(extract_dir)
    glb_root = extract_dir / "Models" / "GLB format"
    for source_name, runtime_name in KENNEY_RUNTIME_ASSETS.items():
        source = glb_root / source_name
        if source.exists():
            shutil.copyfile(source, RUNTIME / runtime_name)


def prepare_polyhaven_materials() -> None:
    runtime_dir = RUNTIME / "materials" / "polyhaven"
    source_dir = SOURCE / "polyhaven" / "materials"
    runtime_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "provider": "Poly Haven",
        "license": "CC0",
        "license_url": "https://polyhaven.com/license",
        "resolution": "1k",
        "format": "jpg",
        "materials": [],
    }
    for item in POLYHAVEN_MATERIALS:
        asset = item["asset"]
        files_json = CACHE / f"polyhaven_{asset}_files.json"
        try:
            download_text(f"https://api.polyhaven.com/files/{asset}", files_json)
            files = json.loads(files_json.read_text(encoding="utf-8"))
            diffuse = files["Diffuse"]["1k"]["jpg"]
            source_path = source_dir / f"{asset}_diff_1k.jpg"
            runtime_path = runtime_dir / item["runtime"]
            download_binary(diffuse["url"], source_path)
            shutil.copyfile(source_path, runtime_path)
            manifest["materials"].append(
                {
                    "role": item["role"],
                    "asset": asset,
                    "runtime": str(runtime_path.relative_to(ROOT)).replace("\\", "/"),
                    "source_url": diffuse["url"],
                    "bytes": diffuse["size"],
                    "md5": diffuse.get("md5", ""),
                }
            )
        except Exception as exc:  # noqa: BLE001 - demo keeps procedural fallback.
            print(f"[warn] Poly Haven material download failed for {asset}: {exc}")

    if manifest["materials"]:
        (runtime_dir / "polyhaven_materials.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def optional_download() -> None:
    """Download small metadata pages only.

    Heavy DEM/DOM/model downloads should be added here as exact, reviewed URLs
    once a concrete deployment area and asset pack are selected.
    """
    meta = SOURCE / "metadata"
    meta.mkdir(parents=True, exist_ok=True)
    for i, (_, _, url) in enumerate(OPEN_SOURCES):
        safe = f"source_{i:02d}.txt"
        try:
            download_text(url.split(" and ")[0], meta / safe)
        except Exception as exc:  # noqa: BLE001 - report and keep script useful offline.
            print(f"[warn] metadata download failed for {url}: {exc}")
    for item in DOWNLOADS:
        try:
            download_binary(item["url"], item["source"])
            item["runtime"].parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(item["source"], item["runtime"])
        except Exception as exc:  # noqa: BLE001 - keep offline fallback useful.
            print(f"[warn] asset download failed for {item['name']}: {exc}")
    try:
        download_binary(KENNEY_FACTORY_KIT["url"], KENNEY_FACTORY_KIT["source"])
        prepare_kenney_runtime()
    except Exception as exc:  # noqa: BLE001 - keep offline fallback useful.
        print(f"[warn] asset download failed for {KENNEY_FACTORY_KIT['name']}: {exc}")
    prepare_polyhaven_materials()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--download", action="store_true", help="download lightweight source metadata pages")
    args = parser.parse_args()

    ensure_dirs()
    write_manifest()
    if args.download:
        optional_download()
    print(f"showcase asset area ready: {ASSET_ROOT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
