"""Bounded five-minute local HTTP concurrency soak; not a production capacity claim."""
import concurrent.futures
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from supplement_helpers import CustomerRun
r=CustomerRun(sys.argv[1],'非Windows补测：真实Qt服务端+临时SQLite，16并发HTTP连接及300秒有界持续请求；非长期生产验收')
base=r.base
local=threading.local()
def call(method,path,body=None,token=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+(token or r.token)},method=method)
 try:
  with urllib.request.urlopen(req,timeout=20) as res:return res.status,json.load(res)
 except urllib.error.HTTPError as err:return err.code,json.load(err)
with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
 results=list(pool.map(lambda i:call('POST','/device',dict(code=f'CONCURRENT{i}',name=f'并发设备{i}',groupId=1)),range(160)))
 ids=[x[1].get('data',{}).get('id') for x in results]
 r.check(482,'16并发新增160设备全部成功且ID唯一，无丢失或覆盖',all(x[0]==200 and x[1].get('code')==0 for x in results) and len(set(ids))==160,dict(success=sum(x[0]==200 for x in results),unique=len(set(ids))))
 duplicates=list(pool.map(lambda i:call('POST','/employee',dict(code=f'9000{i:02d}',name=f'并发员工{i}',phone='13800138000',groupId=1)),range(16)))
 r.check(313,'16并发相同手机号员工仅一条成功，其余明确拒绝',sum(x[0]==200 for x in duplicates)==1 and all(x[0] in (200,400) for x in duplicates),[x[0] for x in duplicates])
 # Independently authenticate 16 sessions, then mix writes and reads.
 tokens=list(pool.map(lambda i:call('POST','/auth/login',dict(username='admin',password='admin123'))[1]['data']['token'],range(16)))
 updates=list(pool.map(lambda i:call('PUT',f'/device/{ids[i]}',dict(name=f'并发更新{i}'),tokens[i%16]),range(160)))
 r.check(482,'16独立登录会话并发更新设备均成功',all(x[0]==200 and x[1].get('code')==0 for x in updates))
 stored=r.data('GET','/device/list?fullModel=1')
 r.check(482,'并发更新后160条设备最终名称与ID逐一对应',len(stored)==160 and all(next(x for x in stored if x['id']==identifier)['name']==f'并发更新{i}' for i,identifier in enumerate(ids)))
 duration=300
 deadline=time.monotonic()+duration
 started=time.monotonic();samples=[];request_count=0;errors=[];latencies=[]
 def worker(i):
  paths=['/healthz','/device/list?page=1&pageSize=100','/device/tree','/statistics/home','/device/list?fullModel=1']
  # API actually exposes /statistics/home; each iteration checks business envelope.
  n=0;own=[];bad=[]
  while time.monotonic()<deadline:
   tick=time.monotonic()
   try:
    status,value=call('GET',paths[(n+i)%len(paths)],token=tokens[i])
    if status!=200 or value.get('code')!=0:bad.append((status,value))
   except Exception as error:bad.append(repr(error))
   own.append(time.monotonic()-tick);n+=1;time.sleep(.12)
  return own,bad
 futures=[pool.submit(worker,i) for i in range(16)]
 while any(not f.done() for f in futures):
  rss=int(subprocess.run(['ps','-o','rss=','-p',sys.argv[4]],capture_output=True,text=True,check=True).stdout.strip())
  samples.append(dict(seconds=round(time.monotonic()-started,1),rssKiB=rss))
  if len(samples)%3==0:print('soak progress',samples[-1],flush=True)
  time.sleep(5)
 for f in futures:
  own,bad=f.result();latencies+=own;errors+=bad
 request_count=len(latencies)
 r.check(482,'300秒16会话持续请求全部返回成功，服务始终存活',not errors and time.monotonic()-started>=duration,dict(requests=request_count,errors=errors[:10]))
 # Report memory and latency observations without turning an arbitrary budget into a requirement.
 latencies.sort();warm=[x['rssKiB'] for x in samples[3:]]
 r.check(482,'持续请求后数据仍完整，SQLite完整性正常',len(r.data('GET','/device/list?fullModel=1'))==160 and sqlite3.connect(sys.argv[3]).execute('PRAGMA integrity_check').fetchone()[0]=='ok')
 detail=dict(durationSeconds=round(time.monotonic()-started,1),requests=request_count,p95Ms=round(latencies[int(len(latencies)*.95)]*1000,2),maxMs=round(max(latencies)*1000,2),rssStartKiB=samples[0]['rssKiB'],rssEndKiB=samples[-1]['rssKiB'],rssMinAfter15sKiB=min(warm),rssMaxAfter15sKiB=max(warm),samples=samples)
 artifact=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/nonwindows-reliability-2026-10-24';artifact.mkdir(exist_ok=True);(artifact/'soak-metrics.json').write_text(json.dumps(detail,ensure_ascii=False,indent=2))
 print(json.dumps({k:v for k,v in detail.items() if k!='samples'}),flush=True)
sys.exit(int(bool(r.failures)))
