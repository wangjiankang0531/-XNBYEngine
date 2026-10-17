"""Verify the shipped EXE independently of source cwd/global Python and annotation styles."""
import argparse
import base64
import copy
import hashlib
import json
import os
import subprocess
import time
from pathlib import Path
from verify_badaling_demo import read_png_rgb

ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--package',default='work/releases/landscape_apps_20261011');parser.add_argument('--fine',action='store_true');parser.add_argument('--exe',default='ht_landscape_editor_0_7.exe');parser.add_argument('--output',default='work/landscape_apps_checks');args=parser.parse_args()
    package=(ROOT/args.package).resolve();out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    exe=package/args.exe;env=os.environ.copy();env.pop('HT_NATIVE_PYTHON',None);env['USERPROFILE']=str(out/'no_global_python_profile')
    document=json.loads((ROOT/'work/landscape_test_layers.json').read_text(encoding='utf-8'))
    sample=out/'layers.json';sample.write_text(json.dumps(document,ensure_ascii=False),encoding='utf-8')
    def run(options):
        # Windows may briefly hold a freshly linked EXE after a child exits.
        # Retry sharing violations only; a renderer failure is never hidden.
        for attempt in range(8):
            try:
                subprocess.run([str(exe),*options],cwd=out,env=env,check=True,timeout=700)
                return
            except OSError as e:
                if getattr(e,'winerror',None)!=32 or attempt==7:raise
            except subprocess.CalledProcessError as e:
                if (e.returncode & 0xffffffff)!=0xc0000043 or attempt==7:raise
            time.sleep(.4)
    roundtrip=out/'roundtrip.json';run(['--validate-layers',f'--layers={sample}',f'--stats={roundtrip}'])
    assert json.loads(roundtrip.read_text(encoding='utf-8'))==document
    common=['--zhumadian-view=close','--scenario-time=0','--width=1280','--height=720']
    if args.fine:
        run([*common,f'--layers={sample}',f'--screenshot={out/"fine.png"}',f'--stats={out/"fine.json"}'])
        stat=json.loads((out/'fine.json').read_text(encoding='utf-8'));lod=stat['native_lod']
        assert lod['chunked_uploads'] and lod['settled'] and not lod['pending'] and not lod['partial_root_drawn']
        assert any(r['tile']==141 and r['depth']>=6 and r['upload_parts']>=8 for r in lod['resident'])
        assert lod['payload_bytes']<=lod['budget_bytes']==512*1048576 and lod['peak_concurrent_payload_bytes']<=lod['budget_bytes']
        assert lod['max_upload_bytes']<=32*1048576
        (out/'fine_metrics.json').write_text(json.dumps({'passed':True,'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'portable_native_lod':lod},indent=2),encoding='utf-8')
        print('PORTABLE PACKAGED PYTHON / ORIGINAL SOURCE / MULTIPART NATIVE LOD PASS');return
    variants={'empty':copy.deepcopy(document),'hidden':copy.deepcopy(document),'zero':copy.deepcopy(document),'styled':copy.deepcopy(document),'wide':copy.deepcopy(document),'custom':copy.deepcopy(document)}
    for l in variants['empty']['layers']:l['features']=[]
    for l in variants['hidden']['layers']:l['visible']=False
    for l in variants['zero']['layers']:
        for f in l['features']:f['style']['opacity']=0;f['style']['fillOpacity']=0
    for l in variants['wide']['layers']:
        for f in l['features']:f['style']['widthPx']=12;f['style']['sizePx']=64;f['style']['pattern']='dot';f['style']['fillOpacity']=.6
    custom=variants['custom']['layers'][0]['features'][0]['style'];custom['icon']='custom';custom['iconData']='data:image/png;base64,'+base64.b64encode((package/'assets/landscape/icons.png').read_bytes()).decode()
    variants['labeled']=copy.deepcopy(document)
    for l in variants['labeled']['layers']:
        for f in l['features']:
            f['label']={'visible':True,'text':f['name'],'fontPx':20,'color':'#FFFFFF','opacity':1,'offset':[12,-24]}
    frames={};hashes={}
    for name,doc in variants.items():
        file=out/f'{name}.json';file.write_text(json.dumps(doc,ensure_ascii=False),encoding='utf-8')
        screenshot=out/f'{name}.png';telemetry=out/f'{name}_stats.json'
        run([*common,'--zhumadian-geometry-cold',f'--layers={file}',f'--screenshot={screenshot}',f'--stats={telemetry}'])
        stat=json.loads(telemetry.read_text(encoding='utf-8'))
        assert stat['scene']=='zhumadian' and stat['source_roots']==2346 and stat['huilian_models']==stat['separate_chemical_models']==0
        assert stat['satellite_context'] and stat['context_fused'] and stat['indices']==11668083 and stat['vertices']==9584599
        assert stat['native_lod']['drawn_child_indices']==0
        w,h,pixels=read_png_rgb(screenshot);assert (w,h)==(1280,720);frames[name]=pixels;hashes[name]=hashlib.sha256(screenshot.read_bytes()).hexdigest()
    assert frames['empty']==frames['hidden']==frames['zero'],'Hidden/zero-alpha annotations changed the scene pixels'
    changed=sum(a!=b for a,b in zip(frames['empty'],frames['styled']));assert changed>1000,changed
    assert hashes['styled']!=hashes['wide'] and hashes['styled']!=hashes['custom']
    assert sum(a!=b for a,b in zip(frames['styled'],frames['labeled']))>500,'Line/region labels are not visible'
    metrics={'passed':True,'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'image_sha256':hashes,'annotation_changed_pixels':changed,'portable_cwd':str(out),'global_python_disabled':True,'roundtrip':True,'source_roots':2346,'chemical_models':0,'huilian_models':0}
    (out/'metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print('LANDSCAPE EXE PORTABILITY / ROUNDTRIP / POINT-LINE-REGION / ALPHA / HIDDEN LAYER / CUSTOM ICON PASS')
if __name__=='__main__':main()
