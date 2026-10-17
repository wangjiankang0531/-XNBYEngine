import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {selectDepth, measureBaseEdges} from '../apps/landscape_web/src/lod_policy.js';

const root=path.dirname(path.dirname(fileURLToPath(import.meta.url)));
// Irregular real source level spacing must never force a coarse/fine loop.
assert.equal(selectDepth(300,2,0,0,7),0);
assert.equal(selectDepth(300,7,0,0,7),3);
assert.equal(selectDepth(300,40,9,4,7),5);
assert.equal(selectDepth(300,10,1.5,6,7,3),5);
assert.equal(selectDepth(300,10,1.5,6,7,20),6);
assert.equal(selectDepth(300,10,1.5,6,7),6);
assert.equal(selectDepth(300,10,3,5,7),5);
assert.equal(selectDepth(300,2.9,1,5,7),0);
const aboveOldAltitudeGate=20*2160/(2*3500*Math.tan(10*Math.PI/360));
assert.equal(selectDepth(500,aboveOldAltitudeGate,0,0,7),3);

const directory=path.join(root,'assets/showcase/runtime/zhumadian/full');
const gltf=JSON.parse(fs.readFileSync(path.join(directory,'zhumadian_full.gltf'),'utf8'));
const primitive=gltf.meshes[0].primitives[0];
const binary=fs.readFileSync(path.join(directory,gltf.buffers[0].uri));
function attribute(index,width,Type){
  const a=gltf.accessors[index],view=gltf.bufferViews[a.bufferView];
  assert.equal(view.byteStride,undefined);
  return new Type(binary.buffer,binary.byteOffset+(view.byteOffset??0)+(a.byteOffset??0),a.count*width);
}
const positions=attribute(primitive.attributes.POSITION,3,Float32Array);
const indices=attribute(primitive.indices,1,Uint32Array);
const catalog=JSON.parse(fs.readFileSync(path.join(root,'data/showcase/zhumadian_native_hierarchy.json'),'utf8'));
assert.equal(indices.length,9166851);
const measured=measureBaseEdges(catalog,positions,indices);
assert.equal(measured.size,2346);
const native=JSON.parse(fs.readFileSync(path.join(root,'work/landscape_apps_checks/fine_metrics.json'),'utf8'));
const target=native.portable_native_lod.quality_targets.find(r=>r.tile===141);
// Native PNG captures use a 2x rasterization target, resolved to 1280x720.
// Compare source metres, rather than treating those sampling pixels as CSS pixels.
const nativeSamplingHeight=JSON.parse(fs.readFileSync(path.join(root,'work/landscape_apps_checks/fine.json'),'utf8')).height*2;
const nativeEdge=target.base_edge_pixels*2*target.distance_m*Math.tan(50*Math.PI/360)/nativeSamplingHeight;
assert.ok(Math.abs(measured.get(141)-nativeEdge)<0.001);
const result={passed:true,source_roots:measured.size,base_indices:indices.length,
  root_141_base_edge_p90_m:measured.get(141),native_root_141_base_edge_p90_m:nativeEdge,
  policy_cases:9,irregular_parent_hysteresis:true,native_sampling_height:nativeSamplingHeight,source:'original complete base mesh'};
fs.writeFileSync(path.join(root,'work/landscape_apps_checks/web_lod_policy.json'),JSON.stringify(result,null,2));
console.log('WEB LOD / ALL 2346 REAL BASE ROOTS / NATIVE QUALITY AGREEMENT PASS');
