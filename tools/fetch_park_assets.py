#!/usr/bin/env python3
"""Fetch the park-scene CC0 assets (Poly Haven) into assets/park/.

Models arrive as .gltf + .bin + textures/ (the engine's tinygltf loader reads
.gltf with external URIs directly). Heavy photoscan trees are decimated
separately by tools/decimate_gltf.py — this script only downloads.

Run from the repo root:  python3 tools/fetch_park_assets.py
Skips files that already exist (idempotent).
"""

import json
import os
import sys
import urllib.request

BASE = "https://dl.polyhaven.org/file/ph-assets/Models/gltf/2k"

MODELS = [
    ("trees/jacaranda",   "jacaranda_tree"),    # 紫花大树 — hero 树(需减面)
    ("trees/island02",    "island_tree_02"),    # 最轻的大树(~1.07M tris,需减面)
    ("grass/medium01",    "grass_medium_01"),   # photoscan 草丛 ~20k tris,直接用
    ("grass/medium02",    "grass_medium_02"),
    ("shrubs/shrub02",    "shrub_02"),          # 灌木 ~30k tris,直接用
    ("props/lamp",        "street_lamp_01"),     # 路灯 31k
    ("props/bench",       "modular_street_seating"),  # 长椅 25k
]


def fetch(url: str, dst: str) -> None:
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        print(f"  skip {os.path.basename(dst)}")
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    print(f"  GET  {url}")
    urllib.request.urlretrieve(url, dst)


def fetch_model(subdir: str, asset: str) -> None:
    root = os.path.join("assets/park", subdir)
    gltf_name = f"{asset}_2k.gltf"
    gltf_path = os.path.join(root, gltf_name)
    print(f"[{asset}]")
    fetch(f"{BASE}/{asset}/{gltf_name}", gltf_path)

    doc = json.load(open(gltf_path))
    uris = set()
    for b in doc.get("buffers", []):
        if "uri" in b:
            uris.add(b["uri"])
    for i in doc.get("images", []):
        if "uri" in i:
            uris.add(i["uri"])

    # Texture URLs live under Models/<ext>/2k/<asset>/<basename> — NOT under
    # the gltf dir the relative URIs imply. The .bin sits beside the .gltf.
    for uri in sorted(uris):
        base = os.path.basename(uri)
        ext = os.path.splitext(base)[1].lstrip(".")
        if ext in ("jpg", "png"):
            url = f"https://dl.polyhaven.org/file/ph-assets/Models/{ext}/2k/{asset}/{base}"
        else:  # .bin
            url = f"{BASE}/{asset}/{uri}"
        fetch(url, os.path.join(root, uri))


TEXTURES = [
    # (subdir, asset, maps) — PBR texture sets, 2k jpg.
    ("ground", "leafy_grass",  ["diff", "nor_gl", "arm"]),   # 草坪
    ("path",   "grass_path_2", ["diff", "nor_gl", "arm"]),   # 步道
]


def fetch_texture_set(subdir: str, asset: str, maps: list) -> None:
    root = os.path.join("assets/park", subdir)
    print(f"[{asset}]")
    for m in maps:
        name = f"{asset}_{m}_2k.jpg"
        fetch(f"https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/{asset}/{name}",
              os.path.join(root, name))


def merge_alpha_maps(subdir: str, asset: str) -> None:
    """Poly Haven foliage uses alphaMode BLEND/MASK but ships the alpha as a
    SEPARATE *_alpha map the gltf never references — diffuse jpgs have no
    alpha channel, so leaves render as solid gray slabs. For every
    *_diff_2k.jpg referenced by the gltf, try the sibling *_alpha_2k.jpg;
    when it exists, composite an RGBA png and rewrite the gltf's image uri.
    mesh_decimator then embeds the png and the runtime alpha-cutoff works."""
    from PIL import Image

    root = os.path.join("assets/park", subdir)
    gltf_path = os.path.join(root, f"{asset}_2k.gltf")
    doc = json.load(open(gltf_path))
    changed = False
    for img in doc.get("images", []):
        uri = img.get("uri", "")
        if "_diff_2k.jpg" not in uri:
            continue
        base = os.path.basename(uri)
        alpha_name = base.replace("_diff_2k.jpg", "_alpha_2k.jpg")
        alpha_path = os.path.join(root, os.path.dirname(uri), alpha_name)
        alpha_url = ("https://dl.polyhaven.org/file/ph-assets/Models/jpg/2k/"
                     f"{asset}/{alpha_name}")
        if not os.path.exists(alpha_path):
            try:
                fetch(alpha_url, alpha_path)
            except Exception:
                continue  # no alpha sidecar for this map — fine (trunk etc.)
        rgba_uri = uri.replace("_diff_2k.jpg", "_rgba_2k.png")
        rgba_path = os.path.join(root, rgba_uri)
        if not os.path.exists(rgba_path):
            diff = Image.open(os.path.join(root, uri)).convert("RGB")
            alpha = Image.open(alpha_path).convert("L").resize(diff.size)
            rgba = diff.copy()
            rgba.putalpha(alpha)
            rgba.save(rgba_path, optimize=True)
            print(f"  merged alpha → {os.path.basename(rgba_path)}")
        img["uri"] = rgba_uri
        changed = True
    if changed:
        json.dump(doc, open(gltf_path, "w"))
        print(f"  {asset}: gltf now references RGBA foliage textures")


def main() -> None:
    if not os.path.isdir("assets"):
        os.makedirs("assets", exist_ok=True)
    for subdir, asset in MODELS:
        fetch_model(subdir, asset)
        merge_alpha_maps(subdir, asset)
    for subdir, asset, maps in TEXTURES:
        fetch_texture_set(subdir, asset, maps)
    print("done.")


if __name__ == "__main__":
    main()
