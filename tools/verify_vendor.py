"""Verify bundled upstream files against the recorded SHA-256 manifest, offline."""
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'vendor_manifest.json').read_text(encoding='utf-8'))
failed = []
count = 0
for upstream in manifest['upstreams']:
    for record in upstream['files']:
        file = root / record['path']
        count += 1
        if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != record['sha256']:
            failed.append(record['path'])
        if record.get('original_copy'):
            original = root / record['original_copy']
            if not original.is_file() or hashlib.sha256(original.read_bytes()).hexdigest() != record['source_sha256']:
                failed.append(record['original_copy'])
if failed:
    print('Missing or modified upstream files:', *failed, sep='\n')
    sys.exit(1)
print(f'OK: {count} upstream files match the manifest (integrity check, not a security audit).')
