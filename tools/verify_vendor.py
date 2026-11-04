"""Verify original upstream bytes in the checkout or a Git revision, offline."""
import argparse
import hashlib
import io
import json
import pathlib
import subprocess
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--git-ref',help='Verify stored Git blobs instead of the working files, e.g. HEAD')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]
if args.git_ref:
    manifest_bytes=subprocess.run(['git','show',f'{args.git_ref}:vendor_manifest.json'],cwd=root,capture_output=True,check=True).stdout
else:
    manifest_bytes=(root/'vendor_manifest.json').read_bytes()
manifest=json.loads(manifest_bytes)
paths=[]
for upstream in manifest['upstreams']:
    for record in upstream['files']:
        paths.append(record['path'])
        if record.get('original_copy'):paths.append(record['original_copy'])
blobs={}
if args.git_ref:
    names=list(dict.fromkeys(paths))
    result=subprocess.run(['git','cat-file','--batch'],cwd=root,input=''.join(f'{args.git_ref}:{p}\n' for p in names).encode(),capture_output=True,check=True)
    stream=io.BytesIO(result.stdout)
    for path in names:
        header=stream.readline().decode().strip()
        if header.endswith(' missing'):
            blobs[path]=None
        else:
            size=int(header.rsplit(' ',1)[1]);blobs[path]=stream.read(size)
            if stream.read(1)!=b'\n':raise RuntimeError('invalid Git batch response')
def contents(path):
    if args.git_ref:return blobs.get(path)
    file=root/path
    return file.read_bytes()if file.is_file()else None
failed=[];count=0
for upstream in manifest['upstreams']:
    for record in upstream['files']:
        count+=1
        data=contents(record['path'])
        if data is None or hashlib.sha256(data).hexdigest()!=record['sha256']:failed.append(record['path'])
        if record.get('original_copy'):
            original=contents(record['original_copy'])
            if original is None or hashlib.sha256(original).hexdigest()!=record['source_sha256']:failed.append(record['original_copy'])
if failed:
    print('Missing or modified upstream files:',*failed,sep='\n');sys.exit(1)
location=f'Git {args.git_ref}'if args.git_ref else 'working tree'
print(f'OK: {count} upstream files match the manifest in {location} (integrity check, not a security audit).')
