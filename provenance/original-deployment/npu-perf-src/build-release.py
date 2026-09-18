"""Rebuild only inference plugins/tracking at -O3 using installed build metadata."""
from pathlib import Path
import subprocess,shlex,concurrent.futures,json
r=Path('/home/baiwen/ax-pipeline');old=r/'build_axcl-aarch64_ci';out=r/'six/npu-release';patch=r/'six/npu-perf-src'
(out/'objects').mkdir(exist_ok=True,parents=True);(out/'plugins').mkdir(exist_ok=True)
jobs=[];groups={}
spec=[('common','plugins/common/npu','ax_plugin_npu_common',r/'plugins/common/npu'),
      ('tracking','.','ax_pipeline_tracking',r),
      ('bytetrack','third-party/ByteTrack','ByteTrack',r/'third-party/ByteTrack'),
      ('pcd','plugins/pcd.axera','ax_plugin_pcd',r/'plugins/pcd.axera'),
      ('yolo26','plugins/yolo26','ax_plugin_yolo26',r/'plugins/yolo26'),
      ('yolov8_split','plugins/yolov8_split','ax_plugin_yolov8_split',r/'plugins/yolov8_split')]
for group,part,target,srcroot in spec:
    base=old/part/'CMakeFiles'/f'{target}.dir'
    flags={}
    for line in (base/'flags.make').read_text().splitlines():
        if ' = ' in line:
            key,val=line.split(' = ',1);flags[key]=shlex.split(val)
    objects=[]
    for obj in sorted(base.rglob('*.cpp.o')):
        src=srcroot/str(obj.relative_to(base))[:-2]
        if src.name=='ax_model_runner_axcl.cpp':src=patch/src.name
        dest=out/'objects'/f'{group}-{len(objects)}.o';objects.append(dest)
        args=['c++','-O3','-DNDEBUG','-fPIC','-pthread','-I'+str(patch),'-I'+str(r/'plugins/common/npu/src/npu/runner/axcl')]+flags.get('CXX_DEFINES',[])+flags.get('CXX_INCLUDES',[])+['-std=c++17','-c',str(src),'-o',str(dest)]
        jobs.append((src,args))
    assert objects,group
    groups[group]=objects
def compile(job):
    src,args=job;p=subprocess.run(args,capture_output=True,text=True)
    if p.returncode:raise RuntimeError(str(src)+'\n'+p.stderr)
    print('COMPILED',src,flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:list(executor.map(compile,jobs))
for group in ['common','tracking','bytetrack']:
    dest=out/f'lib{group}.a'
    if dest.exists():dest.unlink()
    subprocess.run(['ar','rcs',str(dest)]+list(map(str,groups[group])),check=True)
for name in ['pcd','yolo26','yolov8_split']:
    dest=out/'plugins'/f'libax_plugin_{name}.so'
    staged=Path(str(dest)+'.new')
    args=['c++','-shared','-o',str(staged)]+list(map(str,groups[name]))+[str(out/'libcommon.a'),str(out/'libtracking.a'),str(out/'libbytetrack.a'),'-L'+str(old/'deps/ax-video-sdk-build'),'-L/usr/lib/axcl','-lax_video_sdk','-laxcl_rt','-laxcl_npu','-Wl,--allow-shlib-undefined','-Wl,-rpath,'+str(old/'deps/ax-video-sdk-build')+':/usr/lib/axcl']
    subprocess.run(args,check=True);staged.replace(dest);print('LINKED',dest,flush=True)
print('RELEASE_LIBRARIES_READY',flush=True)
