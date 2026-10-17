"""Assemble a Zhumadian-only portable prototype; hard links save local disk space.

Every file is present inside the resulting folder (no runtime source junctions).
Copying the folder to a different disk materializes the source data normally.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def copy_file(source,target,link=False):
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        if link:return
        if hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(target.read_bytes()).digest():return
        shutil.copy2(source,target);return
    if link:
        try:os.link(source,target);return
        except OSError:pass
    shutil.copy2(source,target)
def tree(source,target,exclude=(),link=False):
    count=0;size=0
    for directory,dirs,files in os.walk(source):
        dirs[:]=[d for d in dirs if d not in exclude]
        relative=Path(directory).relative_to(source)
        for name in files:
            if name.endswith(('.pyc','.pdb')):continue
            file=Path(directory)/name;copy_file(file,target/relative/name,link);size+=file.stat().st_size;count+=1
            if count%20000==0:print(source.name,count,flush=True)
    return count,size
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',default='work/releases/landscape_apps_20261011');parser.add_argument('--runtime-only',action='store_true');parser.add_argument('--crt-directory',type=Path);args=parser.parse_args()
    out=ROOT/args.output;out.mkdir(parents=True,exist_ok=True)
    crt=args.crt_directory or out
    if not all((crt/name).is_file() for name in ['msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll']):
        raise RuntimeError('Provide --crt-directory pointing to the installed Microsoft x64 CRT redistribution folder')
    shaderc=ROOT/'build_noexe/vcpkg_installed/x64-windows-noexe'
    copy_file(shaderc/'tools/shaderc/glslc.exe',out/'runtime/shaderc/glslc.exe')
    copy_file(shaderc/'share/shaderc/copyright',out/'runtime/shaderc/LICENSE.txt')
    for file in crt.glob('*.dll'):
        if file.name.startswith(('msvcp140','vcruntime140','concrt140','vccorlib140')):
            if file.parent.resolve()!=out.resolve():copy_file(file,out/file.name)
            copy_file(file,out/'runtime/shaderc'/file.name)
    if args.runtime_only:
        print('PACKAGED SHADER COMPILER / MICROSOFT CRT PREPARED');return
    binary=ROOT/'build_showcase_render_tests/apps/showcase_demo/Release'
    copy_file(binary/'ht_landscape_editor.exe',out/'ht_landscape_editor_0_8.exe')
    for name in ['fmt.dll','libcurl.dll','SDL2.dll','spdlog.dll','vulkan-1.dll','zlib1.dll']:copy_file(binary/name,out/name)
    data_names=['zhumadian.scene.json','zhumadian_full_stage_manifest.json','zhumadian_native_hierarchy.json','zhumadian_context.json','zhumadian_context_color.json','zhumadian_context_fusion.json','zhumadian_texture_pages.json']
    for name in data_names:
        source=ROOT/'data/showcase'/name
        if source.exists():copy_file(source,out/'data/showcase'/name)
    for name in ['full','context','native_hierarchy','pages']:
        source=ROOT/'assets/showcase/runtime/zhumadian'/name
        if source.exists():tree(source,out/'assets/showcase/runtime/zhumadian'/name,exclude=('source',),link=True)
    tree(ROOT/'assets/landscape',out/'assets/landscape')
    copy_file(ROOT/'projects/驻马店图层示例.json',out/'projects/驻马店图层示例.json')
    (out/'projects').mkdir(exist_ok=True)
    # No old Huilian, separate industrial GLB, equipment, pipe or weather assets.
    tools=['convert_zhumadian_native_chunks.py','convert_zhumadian_native_root.py','build_zhumadian_stage_asset.py','build_zhumadian_full_stage.py','landscape_web_server.py','launch_landscape_web.py']
    for name in tools:copy_file(ROOT/'tools'/name,out/'tools'/name)
    tree(ROOT/'apps/landscape_web/dist',out/'apps/landscape_web/dist')
    copy_file(ROOT/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe',out/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe')
    tree(ROOT/'work/osg-build-min/bin',out/'work/osg-build-min/bin',link=True)
    python=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/python'
    for name in ['python.exe','python3.dll','python312.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt']:copy_file(python/name,out/'runtime/python'/name)
    tree(python/'DLLs',out/'runtime/python/DLLs',link=True)
    tree(python/'Lib',out/'runtime/python/Lib',exclude=('site-packages','test','tests','__pycache__'),link=True)
    for name in ['PIL','pillow.libs']:
        if (python/'Lib/site-packages'/name).exists():tree(python/'Lib/site-packages'/name,out/'runtime/python/Lib/site-packages'/name,link=True)
    count,size=tree(ROOT/'assets/驻马店园区',out/'assets/驻马店园区',link=True)
    tree(ROOT/'work/zhumadian_native_chunk_cache',out/'work/zhumadian_native_chunk_cache',link=False)
    # This marker is preparation only. Acceptance results are added separately.
    records=[]
    for name in ['ht_landscape_editor_0_8.exe','runtime/shaderc/glslc.exe','data/showcase/zhumadian_native_hierarchy.json','apps/landscape_web/dist/viewer.js']:
        file=out/name;records.append({'path':name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'bytes':file.stat().st_size})
    (out/'package.json').write_text(json.dumps({'schema':'hongtu.landscape.apps.package.v1','prepared':True,'verified':False,'files':records,'source_files':count,'source_bytes':size,'scene':'zhumadian','native_budget_mib':512,'portable_python':True,'local_disk_policy':'hardlinked original read-only data; self-contained file paths'},indent=2),encoding='utf-8')
    print('LANDSCAPE APPS PREPARED',out,'SOURCE',count,size,flush=True)
if __name__=='__main__':main()
