"""Replay all 0.2.0 regression cases at their exact original operation counts."""
import argparse,json,pathlib,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--exe',required=True);p.add_argument('--output',default='results/replay-original-regressions');p.add_argument('--repeats',type=int,default=21);a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];plan=root/'results/v0.2.0/summary.json'
args=[sys.executable,str(root/'tools/benchmark.py'),'--exe',a.exe,'--output',a.output,'--repeats',str(a.repeats),'--plan',str(plan)]
for r in json.loads(plan.read_text()):
    if r['cstl_vs_baseline']<.95:args+=['--case',':'.join(str(r[k])for k in ['key_type','workload','threads','keys'])]
subprocess.run(args,check=True)
