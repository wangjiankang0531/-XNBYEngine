"""Reopen and render the document saved during the real Windows UI acceptance run.

This gate does not synthesize mouse input or create the fixture. The fixture was
created using the shipped editor's point, line and region controls; the separate
acceptance report records the actual Windows and browser interactions.
"""

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from verify_badaling_demo import read_png_rgb

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layers", default="work/landscape_apps_checks/ui_final6.json")
    parser.add_argument("--package", default="work/releases/landscape_apps_20261011")
    parser.add_argument("--exe", default="ht_landscape_editor_0_7.exe")
    parser.add_argument("--output", default="work/landscape_apps_checks")
    args = parser.parse_args()
    source = (ROOT / args.layers).resolve()
    package = (ROOT / args.package).resolve()
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    exe = package / args.exe
    doc = json.loads(source.read_text(encoding="utf-8"))
    assert len(doc["layers"]) == 1 and doc["layers"][0]["name"] == "自动化验收"
    features = {f["type"]: f for f in doc["layers"][0]["features"]}
    assert len(doc["layers"][0]["features"]) == len(features) == 3
    assert [len(features[t]["positions"]) for t in ("point", "line", "region")] == [1, 2, 3]
    assert features["line"]["positions"][0][0] == -269
    assert features["line"]["style"]["color"] == "#FF8800"
    assert features["line"]["label"]["text"] == "消防路线"
    assert features["region"]["label"]["text"] == "应急集结区"
    assert features["region"]["style"]["fillOpacity"] == 0.6
    assert all(f["label"]["visible"] for f in features.values())
    env = os.environ.copy()
    env.pop("HT_NATIVE_PYTHON", None)
    env["USERPROFILE"] = str(out / "no_global_python_profile")
    roundtrip = out / "ui_roundtrip.json"
    screenshot = out / "ui_saved.png"
    stats = out / "ui_saved_stats.json"
    commands = [
        [str(exe), "--validate-layers", f"--layers={source}", f"--stats={roundtrip}"],
        [str(exe), f"--layers={source}", "--zhumadian-view=close", "--scenario-time=0",
         "--width=1280", "--height=720", f"--screenshot={screenshot}", f"--stats={stats}"],
    ]
    with (out / "ui_roundtrip.log").open("w", encoding="utf-8") as log:
        for command in commands:
            subprocess.run(command, cwd=out, env=env, check=True, timeout=700,
                           stdout=log, stderr=subprocess.STDOUT)
    assert json.loads(roundtrip.read_text(encoding="utf-8")) == doc
    stat = json.loads(stats.read_text(encoding="utf-8"))
    assert stat["scene"] == "zhumadian" and stat["source_roots"] == 2346
    assert stat["satellite_context"] and stat["context_fused"]
    assert stat["huilian_models"] == stat["separate_chemical_models"] == 0
    assert stat["native_lod"]["settled"] and not stat["native_lod"]["partial_root_drawn"]
    w, h, _ = read_png_rgb(screenshot)
    assert (w, h) == (1280, 720)
    result = {
        "passed": True,
        "fixture_origin": "real Windows UI creation, vertex edit, style edit, Ctrl+S",
        "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "document_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "screenshot_sha256": hashlib.sha256(screenshot.read_bytes()).hexdigest(),
        "point_line_region_counts": [1, 2, 3],
        "roundtrip_equal": True,
        "portable_cwd": str(out),
        "native_lod": stat["native_lod"],
    }
    (out / "ui_roundtrip_metrics.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print("ACTUAL UI-SAVED DOCUMENT / REOPEN / PORTABLE NATIVE RENDER PASS")


if __name__ == "__main__":
    main()
