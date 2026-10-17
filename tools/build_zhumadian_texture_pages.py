#!/usr/bin/env python3
"""Preserve original OSGB atlas texels as independently resident texture pages."""
import argparse
import json
import math
from pathlib import Path

import build_zhumadian_full_stage as full
import build_zhumadian_stage_asset as base


def atlas_rect(index, count, width, height):
    columns = math.ceil(math.sqrt(count))
    cell = full.SIZE // columns
    scale = (cell - 8) / max(width, height)
    w, h = max(1, round(width * scale)), max(1, round(height * scale))
    x, y = index % columns * cell + 4, index // columns * cell + 4
    return [x/full.SIZE, y/full.SIZE, (x+w)/full.SIZE, (y+h)/full.SIZE]


def page_bounds(objects):
    bounds = {}
    for obj in objects:
        materials = full.material_images(obj)
        vertices, texture = [], None
        for line in full.text_lines(obj):
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "v":
                vertices.append(tuple(map(float, fields[1:4])))
            elif fields[0] == "usemtl":
                texture = materials[fields[1]]
            elif fields[0] == "f":
                low, high = bounds.setdefault(texture, ([math.inf]*3, [-math.inf]*3))
                for token in fields[1:]:
                    point = vertices[int(token.split('/')[0])-1]
                    for axis in range(3):
                        low[axis] = min(low[axis], point[axis])
                        high[axis] = max(high[axis], point[axis])
        print(f"page bounds {obj.parent.name}", flush=True)
    return bounds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    root = base.repo_root()
    manifest_path = root / 'data/showcase/zhumadian_texture_pages.json'
    if args.build:
        from PIL import Image
        full_manifest = json.loads((root/'data/showcase/zhumadian_full_stage_manifest.json').read_text(encoding='utf-8'))
        objects = [root/'work/zmd_full_stage_convert'/f'batch_{start:04d}'/'roots.obj'
                   for start in range(0, full_manifest['root_count'], full.BATCH)]
        bounds = page_bounds(objects)
        images = list(dict.fromkeys(p for obj in objects for p in full.material_images(obj).values()))
        assert len(images) == full_manifest['source_atlas_pages'] == 113
        output = root/'assets/showcase/runtime/zhumadian/pages'
        output.mkdir(parents=True, exist_ok=True)
        pages = []
        for index, path in enumerate(images):
            destination = output/f'page_{index:03d}.jpg'
            with Image.open(path) as source:
                width, height = source.size
                source.convert('RGB').save(destination, quality=96, subsampling=0)
            pages.append({'id': index, 'path': base.manifest_path(root, destination),
                          'sha256': base.sha256(destination), 'bytes': destination.stat().st_size,
                          'source': base.manifest_path(root, path), 'source_sha256': base.sha256(path),
                          'size': [width, height], 'atlas_uv_rect': atlas_rect(index, len(images), width, height),
                          'bounds_min': bounds[path][0], 'bounds_max': bounds[path][1]})
        manifest_path.write_text(json.dumps({'schema': 'hongtu.zhumadian_texture_pages.v1',
                                'base_manifest_sha256': base.sha256(root/'data/showcase/zhumadian_full_stage_manifest.json'),
                                'resident_page_limit': 4, 'pages': pages}, indent=2), encoding='utf-8')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    assert manifest['base_manifest_sha256'] == base.sha256(root/'data/showcase/zhumadian_full_stage_manifest.json')
    assert len(manifest['pages']) == 113
    for page in manifest['pages']:
        assert base.sha256(root/page['path']) == page['sha256']
        assert base.sha256(root/page['source']) == page['source_sha256']
        assert base.jpeg_dimensions(root/page['path']) == tuple(page['size'])
        assert all(a < b for a, b in zip(page['atlas_uv_rect'][:2], page['atlas_uv_rect'][2:]))
    print('SOURCE TEXTURE PAGES PASS: 113 native-resolution pages; 4 resident slots, full base fallback')


if __name__ == '__main__':
    main()
