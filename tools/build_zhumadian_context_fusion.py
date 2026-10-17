#!/usr/bin/env python3
"""Build a locally dense, ground-guided display terrain; preserve surveyed inputs."""
import argparse
import array
import bisect
import gzip
import json
import math
import statistics
import sys
from pathlib import Path
from build_zhumadian_context import ROOT, SOURCE, OUTPUT, ORIGIN, BOUNDS, sha, relative, dependencies

MANIFEST = ROOT/'data/showcase/zhumadian_context_fusion.json'


def build():
    np, _, proj = dependencies()
    def axis(low, high, near_low, near_high):
        coarse = np.linspace(low, high, 257)
        result = []
        for a, b in zip(coarse[:-1], coarse[1:]):
            subdivisions = 8 if b >= near_low and a <= near_high else 1
            result.extend(np.linspace(a, b, subdivisions, endpoint=False))
        return np.asarray([*result, high])
    xs = axis(BOUNDS[0], BOUNDS[2], -1500, 3500)
    ys = axis(BOUNDS[1], BOUNDS[3], -3400, 4700)
    x, y = np.meshgrid(xs, ys)
    ny, nx = x.shape
    lon, lat = proj.Transformer.from_crs(32650, 4326, always_xy=True).transform(x+ORIGIN[0], y+ORIGIN[1])
    raw = np.full(x.shape, np.nan)
    for latitude, longitude in ((32,113),(32,114),(33,113),(33,114)):
        data = gzip.decompress((SOURCE/f'N{latitude}E{longitude}.hgt.gz').read_bytes())
        side = math.isqrt(len(data)//2)
        hgt = np.frombuffer(data, dtype='>i2').reshape(side, side)
        selected = (lat>=latitude)&(lat<latitude+1)&(lon>=longitude)&(lon<longitude+1)
        px = np.clip((lon[selected]-longitude)*(side-1), 0, side-1)
        py = np.clip((latitude+1-lat[selected])*(side-1), 0, side-1)
        ix, iy = np.minimum(px.astype(int), side-2), np.minimum(py.astype(int), side-2)
        fx, fy = px-ix, py-iy
        a,b,c,d = hgt[iy,ix], hgt[iy,ix+1], hgt[iy+1,ix], hgt[iy+1,ix+1]
        if selected.any() and min(a.min(),b.min(),c.min(),d.min()) <= -32000:
            raise RuntimeError('Native DEM contains nodata')
        raw[selected] = (a*(1-fx)+b*fx)*(1-fy)+(c*(1-fx)+d*fx)*fy
    assert np.isfinite(raw).all()
    raw_path = OUTPUT/'fusion_raw_dem.f32'
    raw.astype('<f4').tofile(raw_path)
    caps = np.full(x.shape, np.inf)
    inputs = [ROOT/'data/showcase/zhumadian_context.json',
              *(SOURCE/f'N{a}E{b}.hgt.gz' for a,b in ((32,113),(32,114),(33,113),(33,114)))]
    ground = None
    for directory, filename in (('full','zhumadian_full'),('geometry_foreground','detail')):
        path = ROOT/f'assets/showcase/runtime/zhumadian/{directory}/{filename}.gltf'
        doc = json.loads(path.read_text(encoding='utf-8'))
        attributes = doc['meshes'][0]['primitives'][0]['attributes']
        binary = path.parent/doc['buffers'][0]['uri']
        def attribute(name):
            acc = doc['accessors'][attributes[name]]
            view = doc['bufferViews'][acc['bufferView']]
            return np.memmap(binary, dtype='<f4', mode='r',
                             offset=view.get('byteOffset',0)+acc.get('byteOffset',0), shape=(acc['count'],3))
        positions = attribute('POSITION')
        index_acc=doc['accessors'][doc['meshes'][0]['primitives'][0]['indices']]
        index_view=doc['bufferViews'][index_acc['bufferView']]
        source_indices=np.memmap(binary,dtype='<u4',mode='r',
                                 offset=index_view.get('byteOffset',0)+index_acc.get('byteOffset',0),
                                 shape=(index_acc['count'],)).reshape(-1,3)
        # Vertex-only caps miss cells crossed by long triangles. A conservative
        # complete projected bounding rectangle stays below every point of a face.
        for start in range(0,len(source_indices),150000):
            triangles=positions[source_indices[start:start+150000]]
            low,high=triangles.min(axis=1),triangles.max(axis=1)
            x0=np.searchsorted(xs,low[:,0],side='right')-1
            y0=np.searchsorted(ys,low[:,1],side='right')-1
            x1=np.searchsorted(xs,high[:,0],side='right')
            y1=np.searchsorted(ys,high[:,1],side='right')
            assert x0.min()>=0 and x1.max()<nx and y0.min()>=0 and y1.max()<ny
            sizes=np.stack([x1-x0,y1-y0],axis=-1)
            for dx_max,dy_max in np.unique(sizes,axis=0):
                selected=(sizes[:,0]==dx_max)&(sizes[:,1]==dy_max)
                # The overview uses a 4 m near clip with source depths near 10 km.
                # A 1 m physical gap can quantize into the same depth value there.
                # Keep a 3 m display clearance, including on upward-facing ground.
                bx,by,floor=x0[selected],y0[selected],low[selected,2]-3
                for dy in range(dy_max+1):
                    for dx in range(dx_max+1):
                        np.minimum.at(caps,(by+dy,bx+dx),floor)
        if directory == 'full':
            sampled = np.asarray(positions[::17])
            normals = attribute('NORMAL')[::17]
            sampled = sampled[normals[:,2]>.85]
            cells = (np.floor((sampled[:,1]-BOUNDS[1])/156.25).astype(int)*256+
                     np.floor((sampled[:,0]-BOUNDS[0])/156.25).astype(int))
            order = np.argsort(cells)
            sampled, cells = sampled[order], cells[order]
            starts = np.r_[0, np.flatnonzero(np.diff(cells))+1, len(cells)]
            parts = []
            for start, end in zip(starts[:-1], starts[1:]):
                values = sampled[start:end]
                floor = np.percentile(values[:,2],15)
                parts.append(values[values[:,2]<=floor+2.5])
            ground = np.concatenate(parts)
        inputs.extend([path, binary])
    gx = np.searchsorted(xs,ground[:,0]).clip(1,nx-1)
    gy = np.searchsorted(ys,ground[:,1]).clip(1,ny-1)
    gx -= np.abs(ground[:,0]-xs[gx-1]) < np.abs(ground[:,0]-xs[gx])
    gy -= np.abs(ground[:,1]-ys[gy-1]) < np.abs(ground[:,1]-ys[gy])
    ids = gy*nx+gx
    order = np.argsort(ids)
    ground, ids = ground[order], ids[order]
    starts = np.r_[0,np.flatnonzero(np.diff(ids))+1,len(ids)]
    seed_ids, seed_heights = [], []
    for start, end in zip(starts[:-1],starts[1:]):
        seed_ids.append(ids[start]); seed_heights.append(np.median(ground[start:end,2])-.65)
    seed_ids, seed_heights = np.asarray(seed_ids), np.asarray(seed_heights)
    nearest = np.full(x.shape,-1,dtype=int)
    nearest.flat[seed_ids] = np.arange(len(seed_ids))
    sx, sy = xs[seed_ids%nx], ys[seed_ids//nx]
    # Jump flooding extends the sampled ground field using physical XY distances,
    # including across nonuniform cells. It is a visual interpolation, not a survey.
    distance = np.full(x.shape,np.inf)
    distance.flat[seed_ids] = 0
    step = 2**int(math.ceil(math.log2(max(nx,ny))))
    while step:
        for dy,dx in ((-step,-step),(-step,0),(-step,step),(0,-step),(0,step),(step,-step),(step,0),(step,step)):
            ty0,ty1 = max(0,-dy),min(ny,ny-dy)
            tx0,tx1 = max(0,-dx),min(nx,nx-dx)
            if ty0>=ty1 or tx0>=tx1:
                continue
            candidate = nearest[ty0+dy:ty1+dy,tx0+dx:tx1+dx].copy()
            valid = candidate>=0
            ci = candidate.clip(0)
            squared = (x[ty0:ty1,tx0:tx1]-sx[ci])**2+(y[ty0:ty1,tx0:tx1]-sy[ci])**2
            improve = valid & (squared<distance[ty0:ty1,tx0:tx1])
            nearest[ty0:ty1,tx0:tx1][improve] = candidate[improve]
            distance[ty0:ty1,tx0:tx1][improve] = squared[improve]
        step//=2
    assert (nearest>=0).all()
    # Gaps in this reality mesh reach 350 m from sampled ground. Keep the inner
    # 500 m at source-guided height, then reconcile the unsurveyed native DEM
    # over another kilometre, avoiding artificial hills inside source gaps.
    weight = np.clip(1-(np.sqrt(distance)-500)/1000,0,1)
    weight = weight*weight*(3-2*weight)
    target = seed_heights[nearest]
    # A cap only inside the source footprint still permits hills immediately
    # in front of cut walls, or in gaps where roofs bias the ground heuristic.
    # Extend the complete source clearance envelope locally, including ordinary
    # ground faces. Native terrain outside this collar remains unchanged.
    source_caps=caps.copy()
    collar_radius=np.where(target-source_caps>8,100.0,150.0)
    collar_nodes=0
    for dy in range(-8,9):
        for dx in range(-8,9):
            ty0,ty1=max(0,-dy),min(ny,ny-dy)
            tx0,tx1=max(0,-dx),min(nx,nx-dx)
            candidate=source_caps[ty0+dy:ty1+dy,tx0+dx:tx1+dx]
            radius=collar_radius[ty0+dy:ty1+dy,tx0+dx:tx1+dx]
            distance_sq=(x[ty0:ty1,tx0:tx1]-x[ty0+dy:ty1+dy,tx0+dx:tx1+dx])**2+(y[ty0:ty1,tx0:tx1]-y[ty0+dy:ty1+dy,tx0+dx:tx1+dx])**2
            improve=(distance_sq<=radius**2)&(candidate<caps[ty0:ty1,tx0:tx1])
            collar_nodes+=int(improve.sum())
            caps[ty0:ty1,tx0:tx1][improve]=candidate[improve]
    heights = raw-ORIGIN[2]+(target-(raw-ORIGIN[2]))*weight
    heights = np.minimum(heights,caps)
    delta = heights-(raw-ORIGIN[2])
    for _ in range(4):
        padded = np.pad(delta,1,mode='edge')
        average = sum(padded[a:a+ny,b:b+nx] for a in range(3) for b in range(3))/9
        delta = np.minimum(average,caps-(raw-ORIGIN[2]))
    heights = raw-ORIGIN[2]+delta
    def sample(px,py,xx,yy,values):
        ix = np.searchsorted(xx,px,side='right')-1
        iy = np.searchsorted(yy,py,side='right')-1
        fx = (px-xx[ix])/(xx[ix+1]-xx[ix]); fy = (py-yy[iy])/(yy[iy+1]-yy[iy])
        a,b,c,d = values[iy,ix],values[iy,ix+1],values[iy+1,ix],values[iy+1,ix+1]
        return np.where(fx+fy<=1,a+(b-a)*fx+(c-a)*fy,d+(c-d)*(1-fx)+(b-d)*(1-fy))
    old = json.loads((OUTPUT/'context.gltf').read_text(encoding='utf-8'))
    old_view = old['bufferViews'][0]
    old_positions = np.memmap(OUTPUT/'context.bin',dtype='<f4',mode='r',offset=old_view.get('byteOffset',0),shape=(257,257,3))
    old_gap = ground[:,2]-sample(ground[:,0],ground[:,1],old_positions[0,:,0],old_positions[:,0,1],old_positions[:,:,2])
    new_gap = ground[:,2]-sample(ground[:,0],ground[:,1],xs,ys,heights)
    assert new_gap.min()>=2.99 and np.median(new_gap)<np.median(old_gap)
    hy,hx = np.gradient(heights,ys,xs)
    normals = np.stack([-hx,-hy,np.ones_like(x)],axis=-1)
    normals/=np.linalg.norm(normals,axis=-1,keepdims=True)
    positions = np.stack([x,y,heights],axis=-1).astype('<f4').reshape(-1,3)
    normals = normals.astype('<f4').reshape(-1,3)
    uv = np.stack([(x-BOUNDS[0])/(BOUNDS[2]-BOUNDS[0]),1-(y-BOUNDS[1])/(BOUNDS[3]-BOUNDS[1])],axis=-1).astype('<f4').reshape(-1,2)
    rows,cols = np.indices((ny-1,nx-1)); a = rows*nx+cols
    indices = np.stack([a,a+1,a+nx,a+1,a+nx+1,a+nx],axis=-1).astype('<u4').reshape(-1)
    parts = [positions,normals,uv,indices]
    binary = OUTPUT/'context_fused.bin'
    views,accessors,offset = [],[],0
    with binary.open('wb') as dest:
        for i,part in enumerate(parts):
            data = part.tobytes(); dest.write(data)
            views.append({'buffer':0,'byteOffset':offset,'byteLength':len(data)})
            accessor = {'bufferView':i,'componentType':5125 if i==3 else 5126,'count':len(part),'type':('VEC3','VEC3','VEC2','SCALAR')[i]}
            if i==0:
                accessor.update(min=part.min(axis=0).tolist(),max=part.max(axis=0).tolist())
            accessors.append(accessor); offset+=len(data)
    old['buffers']=[{'uri':binary.name,'byteLength':offset}]
    old['bufferViews'],old['accessors']=views,accessors
    gltf = OUTPUT/'context_fused.gltf'
    gltf.write_text(json.dumps(old,indent=2),encoding='utf-8')
    samples_path = OUTPUT/'fusion_ground_samples.f32'
    ground.astype('<f4').tofile(samples_path)
    inputs.extend([OUTPUT/'context.gltf',OUTPUT/'context.bin'])
    manifest = {'schema':'hongtu.zhumadian_context_fusion.v1','model_srs':'EPSG:32650','model_origin':ORIGIN,
                'local_bounds':BOUNDS,'axis_counts':[nx,ny],'local_spacing_m':19.53125,'outer_spacing_m':156.25,
                'vertices':len(positions),'indices':len(indices),'triangles':len(indices)//3,
                'ground_samples':len(ground),'ground_anchors':len(seed_ids),'inner_fit_m':500,'falloff_m':1500,
                'source_clearance_collar_m':150,'source_collar_node_updates':collar_nodes,
                'deep_cut_face_collar_m':100,
                'vertical_policy':'native DEM plus unsurveyed ground-guided visual fit; 3m complete projected triangle rectangle clearance cap for distant depth separation',
                'median_ground_gap_before_m':float(np.median(old_gap)),'median_ground_gap_after_m':float(np.median(new_gap)),
                'minimum_sampled_ground_clearance_m':float(new_gap.min()),
                'inputs':[{'path':relative(p),'sha256':sha(p)} for p in inputs],
                'artifacts':[{'path':relative(p),'sha256':sha(p),'bytes':p.stat().st_size} for p in (gltf,binary,raw_path,samples_path)]}
    MANIFEST.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print('FUSION BUILD',len(positions),'vertices;',len(seed_ids),'anchors;',manifest['median_ground_gap_before_m'],'->',manifest['median_ground_gap_after_m'],flush=True)


def check():
    doc=json.loads(MANIFEST.read_text(encoding='utf-8'))
    for item in doc['inputs']+doc['artifacts']:
        assert sha(ROOT/item['path'])==item['sha256'],item['path']
    assert doc['median_ground_gap_after_m']<doc['median_ground_gap_before_m']
    assert doc['minimum_sampled_ground_clearance_m']>=2.99
    assert doc['vertices']==doc['axis_counts'][0]*doc['axis_counts'][1]
    def values(path, offset, count, kind='f'):
        data=array.array(kind)
        with path.open('rb') as source:
            source.seek(offset); data.fromfile(source,count)
        if sys.byteorder!='little': data.byteswap()
        return data
    nx,ny=doc['axis_counts']
    gltf=json.loads((OUTPUT/'context_fused.gltf').read_text(encoding='utf-8'))
    binary=OUTPUT/'context_fused.bin'
    positions=values(binary,gltf['bufferViews'][0]['byteOffset'],doc['vertices']*3)
    normals=values(binary,gltf['bufferViews'][1]['byteOffset'],doc['vertices']*3)
    indices=values(binary,gltf['bufferViews'][3]['byteOffset'],doc['indices'],'I')
    assert all(math.isfinite(v) for v in positions) and all(math.isfinite(v) for v in normals)
    assert max(indices)<doc['vertices'] and len(indices)==(nx-1)*(ny-1)*6
    xs=[positions[i*3] for i in range(nx)]
    ys=[positions[i*nx*3+1] for i in range(ny)]
    assert all(a<b for a,b in zip(xs,xs[1:])) and all(a<b for a,b in zip(ys,ys[1:]))
    samples=values(OUTPUT/'fusion_ground_samples.f32',0,doc['ground_samples']*3)
    gaps=[]
    for i in range(0,len(samples),3):
        px,py,pz=samples[i:i+3]
        ix=bisect.bisect_right(xs,px)-1; iy=bisect.bisect_right(ys,py)-1
        fx=(px-xs[ix])/(xs[ix+1]-xs[ix]); fy=(py-ys[iy])/(ys[iy+1]-ys[iy])
        a,b,c,d=(positions[(iy*nx+ix)*3+2],positions[(iy*nx+ix+1)*3+2],
                 positions[((iy+1)*nx+ix)*3+2],positions[((iy+1)*nx+ix+1)*3+2])
        height=a+(b-a)*fx+(c-a)*fy if fx+fy<=1 else d+(c-d)*(1-fx)+(b-d)*(1-fy)
        gaps.append(pz-height)
    assert min(gaps)>=2.99
    assert abs(statistics.median(gaps)-doc['median_ground_gap_after_m'])<.001
    print('FUSION AUDIT PASS')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',action='store_true')
    args=parser.parse_args()
    if args.build: build()
    check()
