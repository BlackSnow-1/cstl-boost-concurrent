"""Check pointwise throughput evidence against the original map."""
import argparse,json,math,pathlib,sys
p=argparse.ArgumentParser();p.add_argument('summaries',nargs='+');p.add_argument('--max-regression',type=float,default=.05);a=p.parse_args()
if not 0<=a.max_regression<1:p.error('max-regression must be in [0,1)')
rows=[]
for name in a.summaries:rows.extend(json.loads(pathlib.Path(name).read_text()))
if not rows:p.error('no observations')
failed=[]
for r in rows:
    lo,hi=r['baseline_ratio_ci95']
    if not(math.isfinite(lo)and math.isfinite(hi)and lo>=1-a.max_regression and hi>=1):failed.append(r)
if failed:
    for r in failed:print('FAIL',r['key_type'],r['workload'],r['threads'],r['keys'],r['baseline_ratio_ci95'])
    sys.exit(1)
print(f'PASS: {len(rows)}/{len(rows)} pointwise comparisons; min lower95={min(r["baseline_ratio_ci95"][0]for r in rows):.3f}; no upper95 below 1.000.')
