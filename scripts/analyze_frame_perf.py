"""Summarize native frame timings; nested stage columns are inclusive."""
import argparse,csv,json,statistics
from pathlib import Path

def summarize(path,mode=None,tail=None):
    rows=list(csv.DictReader(Path(path).open()))
    rows=[r for r in rows if all(v is not None for v in r.values())]
    if mode is not None:rows=[r for r in rows if int(r['mode'])==mode]
    if tail and rows:
        end=float(rows[-1]['qpc_s']);rows=[r for r in rows if float(r['qpc_s'])>=end-tail]
    if not rows:return dict(file=str(path),samples=0)
    def stats(values):
        v=sorted(values)
        return {k:round(x,3) for k,x in dict(mean=statistics.mean(v),p50=v[len(v)//2],
            p95=v[min(len(v)-1,int(len(v)*.95))],p99=v[min(len(v)-1,int(len(v)*.99))],max=max(v)).items()}
    metrics={k:stats([float(r[k]) for r in rows]) for k in rows[0] if k.endswith('_ms')}
    elapsed=sum(float(r['frame_ms']) for r in rows)/1000
    transactions=len(set(r['transaction'] for r in rows))
    return dict(file=str(path),samples=len(rows),seconds=round(elapsed,3),fps=round(len(rows)/elapsed,2),
        new_game_frames=transactions,new_game_fps=round(transactions/elapsed,2),
        mean_draws=round(statistics.mean(int(r['draws']) for r in rows),1),metrics=metrics)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('files',nargs='+',type=Path)
    p.add_argument('--mode',type=int);p.add_argument('--tail',type=float);p.add_argument('--out',type=Path)
    args=p.parse_args();result=[summarize(f,args.mode,args.tail) for f in args.files]
    output=json.dumps(result,indent=2)
    if args.out:args.out.write_text(output)
    print(output)
