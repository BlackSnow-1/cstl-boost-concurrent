"""Optional 0.3.0 report figure; requires matplotlib and a Chinese font."""
from pathlib import Path
import json,matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
p=Path(__file__).resolve().parents[1]
old=json.loads((p/'results/v0.2.0/summary.json').read_text());new=json.loads((p/'results/replay-original-regressions/summary.json').read_text())
key=lambda r:tuple(r[k]for k in ['key_type','workload','threads','keys'])
lookup={key(r):r for r in new};old=sorted([r for r in old if r['cstl_vs_baseline']<.95],key=lambda r:r['cstl_vs_baseline'])
plt.rcParams['font.sans-serif']=['Microsoft YaHei','DejaVu Sans'];plt.rcParams['axes.unicode_minus']=False
fig,ax=plt.subplots(figsize=(12,8.7),dpi=160);y=np.arange(len(old));height=.32
for offset,data,color,label in [(-height/2,old,'#BB7074','0.2.0（上一轮）'),(height/2,[lookup[key(r)]for r in old],'#267E72','0.3.0（本轮，同一操作次数）')]:
    values=np.array([r['cstl_vs_baseline']for r in data]);lo=np.array([r['baseline_ratio_ci95'][0]for r in data]);hi=np.array([r['baseline_ratio_ci95'][1]for r in data])
    ax.barh(y+offset,values,height,color=color,label=label,xerr=np.vstack([values-lo,hi-values]),error_kw=dict(ecolor='#444444',elinewidth=.7,capsize=2))
    for yy,v,upper in zip(y+offset,values,hi):ax.text(max(v,upper)+.012,yy,f'{v:.3f}×',va='center',fontsize=8)
work={'growth':'增长插入','update':'更新','upsert':'插入或更新','mixed':'80%读/20%写','churn':'删除后重插'}
labels=[f'{"整型"if r["key_type"]=="integer"else"字符串"} · {work[r["workload"]]} · {r["threads"]}线程'+(' · 64热键'if r['keys']==64 else '')for r in old]
ax.set_yticks(y,labels,fontsize=9);ax.invert_yaxis();ax.axvline(1,color='#303943',linewidth=1,linestyle='--');ax.set_xlim(0,1.85);ax.set_xlabel('相对压缩包原版的吞吐比（原版 = 1）');ax.grid(axis='x',alpha=.16);ax.set_axisbelow(True)
for edge in ['top','right','left']:ax.spines[edge].set_visible(False)
ax.set_title('原先 14 个退步场景：定向优化后全部消除显著退步',loc='left',pad=32,fontsize=15,fontweight='bold')
ax.legend(loc='lower right',frameon=False,fontsize=9);fig.text(.985,.017,'条形为吞吐中位数比值，线为逐场景 95% Bootstrap 区间；完整数据见 PERFORMANCE.md。',ha='right',fontsize=8,color='#555555');fig.subplots_adjust(left=.31,right=.985,top=.90,bottom=.09)
fig.savefig(p/'results/performance-parity.png');plt.close(fig)
print('Performance comparison chart generated.')
