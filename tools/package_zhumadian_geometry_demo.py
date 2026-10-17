#!/usr/bin/env python3
"""Snapshot the verified local runtime without replacing a running executable."""
import hashlib
import json
import shutil
import argparse
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=root/'build_showcase_render_tests/apps/showcase_demo/Release'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile',choices=('center','foreground','compact','streamed','context','context-color','context-fusion'),default='center')
args=parser.parse_args()
compact=args.profile=='compact'
fusion=args.profile=='context-fusion'
color=args.profile in ('context-color','context-fusion')
context=args.profile in ('context','context-color','context-fusion')
streamed=args.profile=='streamed'
foreground=args.profile in ('foreground','compact','streamed','context','context-color','context-fusion')
version=('zhumadian_streamed_20261008' if streamed else ('zhumadian_compact_20261008' if compact else
         ('zhumadian_foreground_20261008' if foreground else 'zhumadian_geometry_20261008'))
        )
target=root/'work/releases'/version
if context:
    version='zhumadian_context_color_20261009' if color else 'zhumadian_context_20261009'
    if fusion:
        version='zhumadian_context_fusion_20261009'
    target=root/'work/releases'/version
target.mkdir(parents=True,exist_ok=True)
paths=[source/'showcase_demo.exe',*sorted(source.glob('*.dll'))]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
files=[]
for path in paths:
    dest=target/path.name
    shutil.copy2(path,dest)
    if digest(path)!=digest(dest):
        raise RuntimeError('snapshot copy mismatch')
    files.append({'name':dest.name,'bytes':dest.stat().st_size,'sha256':digest(dest)})
image=root/('build_showcase_render_tests/zhumadian_streaming/paged.png' if streamed else
            ('build_showcase_render_tests/zhumadian_compaction/foreground_compact.png' if compact else
            ('build_showcase_render_tests/zhumadian_foreground/plant_foreground.png' if foreground else
             'build_showcase_render_tests/zhumadian_geometry_detail/plant_refined.png')))
if context:
    image=root/('build_showcase_render_tests/zhumadian_context_color/matched.png' if color else
                'build_showcase_render_tests/zhumadian_context/context.png')
if fusion:
    image=root/'build_showcase_render_tests/zhumadian_context_fusion/plant_fused.png'
delivery=('work/zhumadian_context_color_delivery.png' if color else ('work/zhumadian_context_delivery.png' if context else ('work/zhumadian_streamed_delivery.png' if streamed else
                       ('work/zhumadian_compact_delivery.png' if compact else
                       ('work/zhumadian_foreground_delivery.png' if foreground else 'work/zhumadian_geometry_delivery.png')))))
if fusion:
    delivery='work/zhumadian_context_fusion_delivery.png'
shutil.copy2(image,root/delivery)
manifest={'version':version,'runtime_files':files,
          'source_main_sha256':digest(root/'apps/showcase_demo/main.cpp'),
          'source_root_required':str(root),'screenshot_sha256':digest(image),
          'geometry_manifest_sha256':digest(root/('data/showcase/zhumadian_geometry_foreground.json' if foreground else
                                                'data/showcase/zhumadian_geometry_detail.json'))}
if compact or streamed or context:
    manifest['source_compaction_header_sha256']=digest(root/'asset/include/hongtu/asset/indexed_mesh_compaction.hpp')
(target/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
(target/'README.md').write_text(
    '# Zhumadian geometry detail local prototype\n\n'
    'Run the matching geometry or foreground CMD entry in the project root.\n'
    'This snapshot requires the original project assets/data at the recorded source root.\n'
    'It is a local, non-public demo, not a standalone distribution.\n'
    'All 2346 source roots retain coverage; the recorded manifest specifies child replacements.\n' +
    ('Geometry refinement is asynchronous for the 271 prepared roots, with complete static base fallback.\n'
     if streamed else 'Geometry refinement is static, not recursive streaming or final UE5-level quality.\n') +
    'Use `--zhumadian-geometry=base` for the same-camera base comparison; F8 cycles views.\n',
    encoding='utf-8')
if compact:
    with (target/'README.md').open('a',encoding='utf-8') as readme:
        readme.write('Run `run_zhumadian_compact_demo.cmd`; use `--zhumadian-geometry-buffer=legacy` for buffer comparison.\n'
                     'This version omits unreferenced parent vertices while preserving the exact drawn triangles.\n')
if streamed:
    manifest['geometry_chunks_manifest_sha256']=digest(root/'data/showcase/zhumadian_geometry_chunks.json')
    manifest['streaming_metrics_sha256']=digest(root/'build_showcase_render_tests/zhumadian_streaming/metrics.json')
    manifest['interactive_travel_sha256']=digest(root/'build_showcase_render_tests/zhumadian_streaming/travel.json')
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with (target/'README.md').open('a',encoding='utf-8') as readme:
        readme.write('Run `run_zhumadian_streamed_demo.cmd`; the beauty entry keeps the full static foreground tier.\n'
                     'Fine buffer payload: 192 MiB budget, at most 96 roots, 16 MiB per service call.\n'
                     'These are fine geometry limits, not total CPU/GPU memory or frame-time guarantees.\n'
                     'This is not streaming of all 237453 OSGB hierarchy files.\n')
if context:
    evidence='zhumadian_context_color' if color else 'zhumadian_context'
    if fusion:
        evidence='zhumadian_context_fusion'
    for key,path in [('context_manifest','data/showcase/zhumadian_context.json'),
                     ('context_metrics',f'build_showcase_render_tests/{evidence}/metrics.json'),
                     ('context_travel',f'build_showcase_render_tests/{evidence}/travel.json')]:
        manifest[key+'_sha256']=digest(root/path)
    if color:
        manifest['context_color_manifest_sha256']=digest(root/'data/showcase/zhumadian_context_color.json')
        manifest['context_color_builder_sha256']=digest(root/'tools/build_zhumadian_context_color.py')
    if fusion:
        manifest['context_fusion_manifest_sha256']=digest(root/'data/showcase/zhumadian_context_fusion.json')
        manifest['context_fusion_builder_sha256']=digest(root/'tools/build_zhumadian_context_fusion.py')
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    with (target/'README.md').open('a',encoding='utf-8') as readme:
        readme.write(('Run run_zhumadian_context_color_demo.cmd.\n' if color else 'Run run_zhumadian_context_demo.cmd.\n') +
                     'Contains modified Copernicus Sentinel data 2025 / Mapzen Terrain Tiles.\n'
                     'The 40 km context uses 10 m satellite imagery, not high-resolution equipment textures.\n'
                     'Visual terrain clearance fitting is not surveyed height; raw DEM is preserved separately.\n')
        if color:
            readme.write('Ground-paired display RGB odds correction is bounded and fades over 1.5 km.\n'
                         'Use --zhumadian-context-color=source for the preserved original satellite-color comparison.\n')
        if fusion:
            readme.write('Run run_zhumadian_context_fusion_demo.cmd; use --zhumadian-context-geometry=coarse for comparison.\n'
                         'Locally dense source-guided display heights are unsurveyed; raw DEM remains separate.\n')
print(f'Packaged {len(files)} runtime files: {target}')
