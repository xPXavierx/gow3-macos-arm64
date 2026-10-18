# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import argparse,json,statistics
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args();raw=json.loads((a.directory/'busy.json').read_text());rows=raw['buffers'];assert rows
rows=sorted(rows,key=lambda r:(r['start'],r['end']));intervals=[]
for r in rows:
 assert r['status']==4 and r['start']>0 and r['end']>=r['start']
 if intervals and r['start']<=intervals[-1][1]:intervals[-1][1]=max(intervals[-1][1],r['end'])
 else:intervals.append([r['start'],r['end']])
span=rows[-1]['end']-rows[0]['start'];span=max(r['end'] for r in rows)-rows[0]['start'];active=sum(b-a for a,b in intervals);durations=[(r['end']-r['start'])*1000 for r in rows];long=[v for v in durations if v>2]
result={'scope':'Game-process command-buffer start/end intervals; includes in-buffer waits and HUD, excludes other processes except any preemption inside these intervals. Intervals merged across queues, not summed. Diagnostic process, separate from clean FPS benchmark.','requested_seconds':raw['requested_seconds'],'covered_seconds':span,'buffers':len(rows),'invalid_timestamps':raw['invalid_timestamps'],'gpu_interval_union_seconds':active,'own_command_buffer_interval_percent':100*active/span,'budget_ms_per_60hz_frame':1000*active/span/60,'free_interval_ms_per_60hz_frame':1000*(1-active/span)/60,'buffer_gpu_ms_mean':statistics.mean(durations),'buffer_gpu_ms_max':max(durations),'buffers_over_2_ms':len(long),'buffer_over_2_ms_mean':statistics.mean(long) if long else None,'counter_buffers_used':raw['counter_buffers_used']}
(a.directory/'busy-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
