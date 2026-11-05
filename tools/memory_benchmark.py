"""Compare process memory from inside the child after validating all live values.
Fresh process per sample, no polling or third-party Python dependencies.
"""
import argparse,json,pathlib,random,statistics,subprocess
p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--output',required=True);a=p.parse_args()
exe=str(pathlib.Path(a.exe).resolve());rng=random.Random(20261105);rows=[]
for kind in ['integer','string']:
    for workload in ['insert','growth']:
        for count in [200000,1000000]:
            for repeat in range(3):
                order=['baseline','cstl'];rng.shuffle(order)
                for backend in order:
                    r=subprocess.run([exe,backend,kind,workload,str(count)],capture_output=True,text=True,check=True,timeout=120);m=json.loads(r.stdout)
                    if m['size']!=count or m['checksum']!=count:raise RuntimeError('incorrect map result')
                    rows.append(dict(key_type=kind,workload=workload,elements=count,repeat=repeat,backend=backend,**m))
            print(kind,workload,count,'validated',flush=True)
summary=[]
for kind in ['integer','string']:
    for workload in ['insert','growth']:
        for count in [200000,1000000]:
            data=[r for r in rows if r['key_type']==kind and r['workload']==workload and r['elements']==count]
            rss={b:statistics.median(r['peak_rss']for r in data if r['backend']==b)for b in ['baseline','cstl']}
            private={b:statistics.median(r['private_bytes']-r['before_private_bytes']for r in data if r['backend']==b)for b in ['baseline','cstl']}
            summary.append(dict(key_type=kind,workload=workload,elements=count,median_peak_rss=rss,cstl_vs_baseline=rss['cstl']/rss['baseline'],median_private_delta=private))
pathlib.Path(a.output).write_text(json.dumps(dict(method='Child queries its own OS process peak RSS after all keys are inserted and verified, while the map is still alive. No generated-key vector or copy-out snapshot. Fresh process, 3 repetitions. Includes runtime/code and allocator-retained memory; not an exact standalone container allocation count. Windows additionally reports private-byte delta; non-Windows private-byte field is unavailable (0).',samples=rows,summary=summary),indent=2))
