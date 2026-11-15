#!/usr/bin/env python3
"""Summarize mp_counters.py + mp_ss.sh output. Exclude first 30 s after first client tick.
Usage: mp_summary.py RESULT_DIR (cli.log, server.log, ss.log).
Data age matches per-stream tick sequence, without changing the wire protocol.
"""
import json, re, statistics, sys, time
from pathlib import Path
p=Path(sys.argv[1])
# socket, seq, start, end, lock, io, longest, bytes, calls, entities
def load(file, side):
 out=[]
 for line in file.read_text(errors='replace').splitlines():
  m=re.search(r'MP '+side+r' ((?:-?\d+\s+){9}-?\d+)',line)
  if m: out.append(list(map(int,m[1].split())))
 return out
c=load(p/'cli.log','client'); s=load(p/'server.log','server')
assert len(c)>2, 'no client tick counters'
# First accepted socket is the graphical client (observer connects later).
sock=s[0][0]; s=[r for r in s if r[0]==sock]; byseq={r[1]:r for r in s}
cut=c[0][3]+30e6; c=[r for r in c if r[3]>=cut]; assert len(c)>2
start=c[0][3]; end=c[-1][3]; sr=[r for r in s if start<=r[2]<=end]
ages=[(r[3]-byseq[r[1]][2])/1000 for r in c if r[1] in byseq]
queues={'send_max':0,'recv_max':0}; rwnd=[]
peer=None; selected=False; active=False
epoch_offset=time.time()-time.monotonic()
for line in (p/'ss.log').read_text().splitlines():
 cols=line.split()
 if cols and cols[0]=='TIME':
  sampled=(float(cols[1])-epoch_offset)*1e6
  active=start<=sampled<=end
 if cols and cols[0]=='ESTAB' and len(cols)>4:
  server=cols[3].endswith(':12345')
  if server and peer is None: peer=cols[4]
  selected=(cols[4] if server else cols[3])==peer
  if selected and active:
   key='send_max' if server else 'recv_max'
   queues[key]=max(queues[key],int(cols[2] if server else cols[1]))
 elif selected and active:
  rwnd+=re.findall(r'rwnd_limited:\S+',line)
result=dict(ticks_s=(len(c)-1)*1e6/(end-start),max_gap_ms=max(b[3]-a[3] for a,b in zip(c,c[1:]))/1000,
 lock_ms_tick=statistics.mean(r[4] for r in c)/1000,recv_ms_tick=statistics.mean(r[5] for r in c)/1000,
 creatures_tick=statistics.mean(r[9] for r in c),server_bytes_tick=statistics.mean(r[7] for r in sr),
 longest_send_ms=max(r[6] for r in sr)/1000,age_max_ms=max(ages),age_mean_ms=statistics.mean(ages),
 duration_s=(end-start)/1e6,**queues,rwnd_limited_last=rwnd[-4:])
print(json.dumps(result,indent=2))
