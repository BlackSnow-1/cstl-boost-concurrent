"""Reproducible same-binary comparison, randomized backend order, correctness checks.
Usage: python tools/benchmark.py --exe build-release/cstl_compare.exe --output results
"""
import argparse,csv,hashlib,json,pathlib,random,statistics,subprocess,time,math,platform,os
p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--output',default='results');p.add_argument('--repeats',type=int,default=7);p.add_argument('--target',type=float,default=.06);p.add_argument('--case',action='append',help='Filter: key_type:workload:threads:keys (repeatable)');a=p.parse_args()
if a.repeats<3 or a.target<=0: p.error('repeats must be >=3 and target must be positive')
out=pathlib.Path(a.output);out.mkdir(parents=True,exist_ok=True)
rng=random.Random(20261104);backends=['baseline','boost','cstl'];raw=[];summary=[]
cases=[]
for t in [1,4,12]:
    for work in ['read','miss','update','upsert','mixed','churn','insert','growth']:cases.append(('integer',work,t,65536))
    for work in ['update','upsert','mixed']:cases.append(('integer',work,t,64))
    for work in ['read','update','mixed','insert','growth']:cases.append(('string',work,t,65536))
if a.case:
    wanted=set(a.case)
    cases=[c for c in cases if ':'.join(map(str,c)) in wanted]
    if len(cases)!=len(wanted): p.error('unknown case filter')
def execute(backend,case,ops):
    keytype,work,threads,keys=case
    r=subprocess.run([str(pathlib.Path(a.exe).resolve()),backend,work,str(threads),str(ops),str(keys),keytype],capture_output=True,text=True,timeout=120,check=True)
    seconds,mops,checksum,value_sum=r.stdout.strip().split(',')
    return dict(seconds=float(seconds),mops=float(mops),checksum=int(checksum),value_sum=int(value_sum))
def ci(xs,ys):
    r=random.Random(42);ratios=[];n=len(xs)
    for _ in range(2000):
        indexes=[r.randrange(n)for i in range(n)]
        ratios.append(statistics.median(xs[i]for i in indexes)/statistics.median(ys[i]for i in indexes))
    ratios.sort();return [ratios[50],ratios[1950]]
with (out/'raw.csv').open('w',newline='') as f:
    cols=['key_type','workload','threads','keys','ops_per_thread','repeat','order','backend','seconds','mops','checksum','value_sum']
    writer=csv.DictWriter(f,fieldnames=cols);writer.writeheader()
    for index,case in enumerate(cases):
        pilots=[execute(b,case,50000)['seconds'] for b in backends]
        ops=max(20000,min(2000000,int(50000*a.target/statistics.median(pilots))))
        # Use a multiple of ten so mixed operation counts remain exact.
        ops=ops//10*10
        samples={b:[]for b in backends}
        for repeat in range(a.repeats):
            order=backends.copy();rng.shuffle(order)
            for position,b in enumerate(order):
                r=execute(b,case,ops);samples[b].append(r['mops'])
                row=dict(key_type=case[0],workload=case[1],threads=case[2],keys=case[3],ops_per_thread=ops,repeat=repeat,order=position,backend=b,**r)
                writer.writerow(row);f.flush();raw.append(row)
        med={b:statistics.median(v)for b,v in samples.items()}
        result=dict(key_type=case[0],workload=case[1],threads=case[2],keys=case[3],ops_per_thread=ops,median_mops=med,cstl_vs_boost=med['cstl']/med['boost'],cstl_vs_baseline=med['cstl']/med['baseline'],boost_ratio_ci95=ci(samples['cstl'],samples['boost']),baseline_ratio_ci95=ci(samples['cstl'],samples['baseline']),samples=samples)
        summary.append(result);(out/'summary.json').write_text(json.dumps(summary,indent=2))
        print(f'{index+1}/{len(cases)} {case}: facade/native={result["cstl_vs_boost"]:.3f}, facade/baseline={result["cstl_vs_baseline"]:.3f}',flush=True)
metadata=dict(timestamp=time.strftime('%Y-%m-%dT%H:%M:%S%z'),platform=platform.platform(),processor=os.environ.get('PROCESSOR_IDENTIFIER'),logical_processors=os.cpu_count(),repeats=a.repeats,target_seconds=a.target,seed=20261104,compiler='MSVC 19.44.35213 x64 /O2 /Ob2 /DNDEBUG C++17',rows=len(raw),cases=len(cases),binary_sha256=hashlib.sha256(pathlib.Path(a.exe).read_bytes()).hexdigest(),facade_sha256=hashlib.sha256((pathlib.Path(__file__).resolve().parents[1]/'include/cstl/concurrent_unordered_map.hpp').read_bytes()).hexdigest(),visitor_sha256=hashlib.sha256((pathlib.Path(__file__).resolve().parents[1]/'include/cstl/detail/map_value_visitor.hpp').read_bytes()).hexdigest(),notes='Single machine; randomized sequential processes; pilot calibration; no CPU pinning; confidence intervals are paired bootstrap of median throughput ratios, not proof of equivalence.')
(out/'environment.json').write_text(json.dumps(metadata,indent=2))
print('DONE',len(raw),'validated samples',flush=True)
