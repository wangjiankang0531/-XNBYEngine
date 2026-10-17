#!/bin/bash
# 一键重建园区资产:下载 Poly Haven CC0 模型/纹理 → 减面出运行时 .glb。
# 需要先构建 mesh_decimator:cmake --build build --target mesh_decimator
set -e
cd "$(dirname "$0")/.."
python3 tools/fetch_park_assets.py
DEC=./build/tools/mesh_decimator/mesh_decimator
$DEC assets/park/trees/jacaranda/jacaranda_tree_2k.gltf assets/park/trees/jacaranda_tree.glb --ratio=0.03 --keep=leaves:0.06
$DEC assets/park/trees/island02/island_tree_02_2k.gltf  assets/park/trees/island_tree_02.glb  --ratio=0.08 --keep=leaves:0.12
$DEC assets/park/grass/medium01/grass_medium_01_2k.gltf assets/park/grass/grass_medium_01.glb --ratio=0.6
$DEC assets/park/grass/medium02/grass_medium_02_2k.gltf assets/park/grass/grass_medium_02.glb --ratio=0.6
$DEC assets/park/shrubs/shrub02/shrub_02_2k.gltf        assets/park/shrubs/shrub_02.glb       --ratio=0.5
$DEC assets/park/trees/island02/island_tree_02_2k.gltf  assets/park/trees/island_tree_02_lod1.glb --ratio=0.008 --keep=leaves:0.012 --error=0.05
$DEC assets/park/trees/jacaranda/jacaranda_tree_2k.gltf assets/park/trees/jacaranda_tree_lod1.glb --ratio=0.005 --error=0.6 --sloppy
$DEC assets/park/props/lamp/street_lamp_01_2k.gltf      assets/park/props/street_lamp_01.glb  --ratio=0.5
$DEC assets/park/props/bench/modular_street_seating_2k.gltf assets/park/props/bench.glb       --ratio=0.5
echo "park assets ready."
