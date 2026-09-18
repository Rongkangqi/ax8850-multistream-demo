#!/usr/bin/env python3
"""Resolve repository-relative assets and runtime plugins, without changing the template."""
import argparse
import json
import os
from pathlib import Path
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parents[1]
def resolve_config(source, runtime):
    config=json.loads(Path(source).read_text(encoding='utf-8-sig'))
    channels=config.get('channels',[])
    if len(channels)!=6:
        raise ValueError('This 3x2 demo requires exactly six channels.')
    def local(value):
        p=Path(value).expanduser()
        p=p if p.is_absolute() else ROOT/p
        if not p.is_file():raise FileNotFoundError(p)
        with p.open('rb') as f:
            if f.read(80).startswith(b'version https://git-lfs.github.com/spec/v1'):
                raise ValueError(f'{p}: Git LFS pointer only; run git lfs pull')
        return str(p.resolve())
    names=[]
    for c in channels:
        names.append(c['name'])
        if float(c.get('source_fps',0))<=0:raise ValueError('source_fps must be positive')
        if not urlsplit(c['input']).scheme:c['input']=local(c['input'])
        c['model']=local(c['model'])
        if 'plugin' in c:
            c['plugin']=local(runtime/'lib/plugins'/Path(c['plugin']).name)
        options=c.get('plugin_options',{})
        if 'model_path' in options:options['model_path']=local(options['model_path'])
    if len(set(names))!=6:raise ValueError('Channel names must be unique')
    return config

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,default=ROOT/'configs/six.json')
    p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--output',type=Path,default=ROOT/'run/config.json')
    args=p.parse_args()
    try:config=resolve_config(args.config,args.runtime.resolve())
    except (ValueError,KeyError,OSError) as e:p.exit(1,f'Configuration error: {e}\n')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    tmp=args.output.with_suffix('.tmp')
    tmp.write_text(json.dumps(config,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    os.replace(tmp,args.output)
    print(args.output)
