#!/usr/bin/env python3
"""Fetch a bounded Sentinel/Mapzen crop and build georeferenced Zhumadian context."""
import argparse
import gzip
import hashlib
import json
import math
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/showcase/runtime/zhumadian/context/source"
OUTPUT = SOURCE.parent
MANIFEST = ROOT / "data/showcase/zhumadian_context.json"
ORIGIN = (225319.5, 3648440.1, 30.9)
BOUNDS = (-19000.0, -19350.0, 21000.0, 20650.0)
SIDE = 257
SCENE_ID = "S2B_50SKB_20251028_0_L2A"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path):
    return path.relative_to(ROOT).as_posix()


def dependencies():
    sys.path.insert(0, str(ROOT / "work/geospatial_deps"))
    import numpy as np
    import rasterio
    import pyproj
    return np, rasterio, pyproj


def fetch():
    np, rio, proj = dependencies()
    from rasterio.windows import from_bounds
    from rasterio.enums import Resampling
    from rasterio.transform import from_bounds as transform_from_bounds
    import certifi
    SOURCE.mkdir(parents=True, exist_ok=True)
    item = SOURCE / "sentinel_item.json"
    if item.exists():
        scene = json.loads(item.read_text(encoding="utf-8"))
    else:
        url = f"https://earth-search.aws.element84.com/v1/collections/sentinel-2-l2a/items/{SCENE_ID}"
        with urllib.request.urlopen(url, timeout=40) as response:
            scene = json.load(response)
        item.write_text(json.dumps(scene, indent=2), encoding="utf-8")
    if scene["id"] != SCENE_ID or scene["properties"].get("proj:epsg") != 32650:
        raise RuntimeError("Pinned source scene changed")
    href = scene["assets"]["visual"]["href"]
    crop = SOURCE / "sentinel_crop.tif"
    left, bottom, right, top = (BOUNDS[0]+ORIGIN[0], BOUNDS[1]+ORIGIN[1],
                               BOUNDS[2]+ORIGIN[0], BOUNDS[3]+ORIGIN[1])
    if not crop.exists():
        with rio.Env(GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR", GDAL_HTTP_TIMEOUT=40,
                     CURL_CA_BUNDLE=certifi.where(),
                     GDAL_HTTP_MAX_RETRY=2, CPL_VSIL_CURL_ALLOWED_EXTENSIONS=".tif"):
            with rio.open(href) as src:
                if src.crs.to_epsg() != 32650 or src.count != 3:
                    raise RuntimeError("Sentinel source projection/channels differ")
                if not (src.bounds.left <= left and src.bounds.right >= right and
                        src.bounds.bottom <= bottom and src.bounds.top >= top):
                    raise RuntimeError("Sentinel scene does not cover the whole context")
                window = from_bounds(left, bottom, right, top, src.transform)
                pixels = src.read(window=window, out_shape=(3,4096,4096), resampling=Resampling.bilinear)
                if (np.all(pixels == 0, axis=0)).mean() > 0.001:
                    raise RuntimeError("Sentinel context has missing imagery")
                with rio.open(crop, "w", driver="GTiff", width=4096, height=4096, count=3,
                              dtype="uint8", crs="EPSG:32650",
                              transform=transform_from_bounds(left,bottom,right,top,4096,4096),
                              tiled=True, blockxsize=256, blockysize=256, compress="deflate") as dest:
                    dest.write(pixels)
        print("SENTINEL CROP", scene["id"], flush=True)
    for latitude,longitude in ((32,113),(32,114),(33,113),(33,114)):
        filename = f"N{latitude}E{longitude}.hgt.gz"
        path = SOURCE / filename
        if not path.exists():
            url = f"https://elevation-tiles-prod.s3.amazonaws.com/skadi/N{latitude}/{filename}"
            with urllib.request.urlopen(url, timeout=40) as response:
                data = response.read()
            raw = gzip.decompress(data)
            side = math.isqrt(len(raw)//2)
            if side*side*2 != len(raw) or side not in (1201,3601):
                raise RuntimeError("Unexpected Mapzen HGT shape")
            path.write_bytes(data)
            print("DEM TILE", filename, side, flush=True)


def build():
    np, rio, proj = dependencies()
    from PIL import Image
    import PIL
    OUTPUT.mkdir(parents=True, exist_ok=True)
    scene = json.loads((SOURCE/"sentinel_item.json").read_text(encoding="utf-8"))
    with rio.open(SOURCE/"sentinel_crop.tif") as src:
        rgb = src.read().transpose(1,2,0)
        if src.crs.to_epsg() != 32650:
            raise RuntimeError("Context crop CRS differs from model CRS")
        texture = OUTPUT/"context.jpg"
        Image.fromarray(rgb).save(texture,quality=95,subsampling=0)
    xs = np.linspace(BOUNDS[0], BOUNDS[2], SIDE)
    ys = np.linspace(BOUNDS[1], BOUNDS[3], SIDE)
    x, y = np.meshgrid(xs,ys)
    transform = proj.Transformer.from_crs(32650,4326,always_xy=True)
    lon,lat = transform.transform(x+ORIGIN[0],y+ORIGIN[1])
    if not (lon.min()>=113 and lon.max()<=115):
        raise RuntimeError(f"DEM longitude coverage incomplete: {lon.min()}..{lon.max()}")
    raw_dem = np.full(x.shape,np.nan)
    for latitude,longitude in ((32,113),(32,114),(33,113),(33,114)):
        data = gzip.decompress((SOURCE/f"N{latitude}E{longitude}.hgt.gz").read_bytes())
        side = math.isqrt(len(data)//2)
        hgt = np.frombuffer(data,dtype=">i2").reshape(side,side)
        selected=(lat>=latitude)&(lat<latitude+1)&(lon>=longitude)&(lon<longitude+1)
        if not selected.any():
            continue
        px=np.clip((lon[selected]-longitude)*(side-1),0,side-1)
        py=np.clip((latitude+1-lat[selected])*(side-1),0,side-1)
        ix=np.minimum(px.astype(int),side-2); iy=np.minimum(py.astype(int),side-2)
        fx=px-ix; fy=py-iy
        a,b,c,d=hgt[iy,ix],hgt[iy,ix+1],hgt[iy+1,ix],hgt[iy+1,ix+1]
        if np.min([a.min(),b.min(),c.min(),d.min()]) <= -32000:
            raise RuntimeError("DEM contains nodata")
        raw_dem[selected]=(a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy
    if not np.isfinite(raw_dem).all():
        raise RuntimeError("DEM coverage incomplete")
    raw_dem.astype("<f4").tofile(OUTPUT/"raw_dem.f32")
    heights=raw_dem-ORIGIN[2]
    caps=np.full((SIDE,SIDE),np.inf)
    inputs=[]
    # Fit the visual underlay below both base and fine source geometry. Preserve
    # the original DEM independently: fitted heights are not survey elevations.
    for directory,filename in (("full","zhumadian_full"),("geometry_foreground","detail")):
        path=ROOT/f"assets/showcase/runtime/zhumadian/{directory}/{filename}.gltf"
        doc=json.loads(path.read_text(encoding="utf-8"))
        primitive=doc["meshes"][0]["primitives"][0]
        acc=doc["accessors"][primitive["attributes"]["POSITION"]]
        view=doc["bufferViews"][acc["bufferView"]]
        binary=path.parent/doc["buffers"][view["buffer"]]["uri"]
        p=np.memmap(binary,dtype="<f4",mode="r",offset=view.get("byteOffset",0)+acc.get("byteOffset",0),
                    shape=(acc["count"],3))
        cx=np.floor((p[:,0]-BOUNDS[0])/(BOUNDS[2]-BOUNDS[0])*(SIDE-1)).astype(int)
        cy=np.floor((p[:,1]-BOUNDS[1])/(BOUNDS[3]-BOUNDS[1])*(SIDE-1)).astype(int)
        if cx.min()<0 or cx.max()>=SIDE-1 or cy.min()<0 or cy.max()>=SIDE-1:
            raise RuntimeError("Source geometry escapes background")
        for dy,dx in ((0,0),(1,0),(0,1),(1,1)):
            np.minimum.at(caps,(cy+dy,cx+dx),p[:,2]-1.0)
        inputs.append({"path":relative(binary),"sha256":sha(binary)})
    finite=np.isfinite(caps)
    delta=np.where(finite,np.minimum(0,caps-heights),0)
    core=delta.copy()
    for _ in range(4):
        padded=np.pad(delta,1,mode="edge")
        average=sum(padded[dy:dy+SIDE,dx:dx+SIDE] for dy in range(3) for dx in range(3))/9
        delta=np.minimum(average,core)
    heights+=delta
    step=(BOUNDS[2]-BOUNDS[0])/(SIDE-1)
    hy,hx=np.gradient(heights,step,step)
    normals=np.stack([-hx,-hy,np.ones_like(x)],axis=-1)
    normals/=np.linalg.norm(normals,axis=-1,keepdims=True)
    positions=np.stack([x,y,heights],axis=-1).astype("<f4").reshape(-1,3)
    normals=normals.astype("<f4").reshape(-1,3)
    uv=np.stack([(x-BOUNDS[0])/(BOUNDS[2]-BOUNDS[0]),
                 1-(y-BOUNDS[1])/(BOUNDS[3]-BOUNDS[1])],axis=-1).astype("<f4").reshape(-1,2)
    corners=np.arange((SIDE-1)*(SIDE-1)).reshape(SIDE-1,SIDE-1)
    rows,cols=np.indices(corners.shape); a=rows*SIDE+cols
    indices=np.stack([a,a+1,a+SIDE,a+1,a+SIDE+1,a+SIDE],axis=-1).astype("<u4").reshape(-1)
    parts=[positions,normals,uv,indices]
    views=[]; accessors=[]; offset=0
    binary=OUTPUT/"context.bin"
    with binary.open("wb") as dest:
        for i,part in enumerate(parts):
            data=part.tobytes(); dest.write(data)
            views.append({"buffer":0,"byteOffset":offset,"byteLength":len(data)})
            accessor={"bufferView":i,"componentType":5125 if i==3 else 5126,
                      "count":len(part),"type":("VEC3","VEC3","VEC2","SCALAR")[i]}
            if i==0:
                accessor["min"]=part.min(axis=0).tolist(); accessor["max"]=part.max(axis=0).tolist()
            accessors.append(accessor); offset+=len(data)
    gltf={"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},
                                   "indices":3,"material":0}]}],
          "buffers":[{"uri":"context.bin","byteLength":offset}],"bufferViews":views,"accessors":accessors,
          "images":[{"uri":"context.jpg"}],"textures":[{"source":0}],
          "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},
                                              "metallicFactor":0,"roughnessFactor":1}}]}
    (OUTPUT/"context.gltf").write_text(json.dumps(gltf,indent=2),encoding="utf-8")
    rx,ry=proj.Transformer.from_crs(4326,32650,always_xy=True).transform(lon,lat)
    error=np.max(np.hypot(rx-(x+ORIGIN[0]),ry-(y+ORIGIN[1])))
    sources=[{"path":relative(p),"sha256":sha(p),"bytes":p.stat().st_size} for p in SOURCE.iterdir() if p.is_file()]
    artifacts=[{"path":relative(p),"sha256":sha(p),"bytes":p.stat().st_size}
               for p in (OUTPUT/"context.gltf",binary,texture,OUTPUT/"raw_dem.f32")]
    manifest={"schema":"hongtu.zhumadian_context.v1","model_srs":"EPSG:32650","model_origin":ORIGIN,
              "local_bounds":BOUNDS,"grid_side":SIDE,"vertices":len(positions),"triangles":len(indices)//3,
              "imagery":{"source":"Copernicus Sentinel-2 L2A","item_id":scene["id"],
                         "datetime":scene["properties"]["datetime"],"native_resolution_m":10,
                         "url":scene["assets"]["visual"]["href"],"texture_size":[4096,4096]},
              "elevation":{"source":"Mapzen Terrain Tiles, Skadi HGT",
                           "urls":[f"https://elevation-tiles-prod.s3.amazonaws.com/skadi/N{a}/N{a}E{b}.hgt.gz"
                                   for a,b in ((32,113),(32,114),(33,113),(33,114))],
                           "raw_range_m":[float(raw_dem.min()),float(raw_dem.max())],
                           "fitted_local_range_m":[float(heights.min()),float(heights.max())],
                           "vertical_policy":"raw DEM minus model origin; underlay clearance fit, unsurveyed",
                           "fitted_nodes":int(np.count_nonzero(delta)),"max_visual_lowering_m":float(-delta.min())},
              "projection_roundtrip_max_error_m":float(error),"sources":sources,"geometry_inputs":inputs,
              "artifacts":artifacts,"dependencies":{"rasterio":rio.__version__,"pyproj":proj.__version__,
                                                   "numpy":np.__version__,"Pillow":PIL.__version__},
              "attribution":"Contains modified Copernicus Sentinel data 2025; Mapzen Terrain Tiles",
              "license_sources":["https://registry.opendata.aws/sentinel-2-l2a-cogs/",
                                 "https://github.com/tilezen/joerd/blob/master/docs/attribution.md"]}
    MANIFEST.write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print("CONTEXT BUILD", manifest["elevation"], "projection error",error,flush=True)


def check():
    doc=json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert doc["model_srs"]=="EPSG:32650" and doc["model_origin"]==list(ORIGIN)
    assert doc["projection_roundtrip_max_error_m"]<0.001
    for entry in doc["sources"]+doc["artifacts"]+doc["geometry_inputs"]:
        assert sha(ROOT/entry["path"])==entry["sha256"],entry["path"]
    assert doc["vertices"]==SIDE*SIDE and doc["triangles"]==2*(SIDE-1)**2
    print("CONTEXT AUDIT PASS",doc["imagery"]["item_id"],doc["vertices"],doc["triangles"])


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch",action="store_true")
    parser.add_argument("--build",action="store_true")
    args=parser.parse_args()
    if args.fetch: fetch()
    if args.build: build()
    if not args.fetch or args.build: check()
