#!/usr/bin/env python3
"""Verify the model/video files copied from the working board."""
import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
rows=json.loads((root/'provenance/assets-manifest.json').read_text());bad=[]
for row in rows:
    p=root/row['path'];h=hashlib.sha256()
    if p.is_file():
        with p.open('rb') as f:
            for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
    good=p.is_file() and p.stat().st_size==row['bytes'] and h.hexdigest()==row['sha256']
    print(('OK   ' if good else 'FAIL ')+row['path'],flush=True)
    if not good:bad.append(row['path'])
sys.exit(bool(bad))
