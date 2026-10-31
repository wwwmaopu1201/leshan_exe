"""Aggregate summary must cover all filtered tasks rather than only the current page."""
import json
from pathlib import Path
import sqlite3
import sys
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 with urllib.request.urlopen(req,timeout=8) as response:return json.load(response)['data']
def check(name,passed,actual=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=212,name=name,result='PASS' if passed else 'FAIL',actual=actual,scope='真实Qt服务端统计API；25个已知状态任务的临时SQLite夹具，独立数学期望'),ensure_ascii=False)+'\n')
 print('212','PASS' if passed else 'FAIL',name,actual,flush=True);failures+=not passed
token=api('POST','/auth/login',dict(username='admin',password='admin123'))['token'];device=api('POST','/device',dict(code='summary',name='汇总设备'))['id'];db=sqlite3.connect(sys.argv[3])
for n in range(25):
 status='completed' if n<10 else 'failed' if n<15 else 'uploading' if n==15 else 'waiting';progress=100 if n<10 else 20 if n<15 else 60 if n==15 else 0
 db.execute('INSERT INTO upload_tasks(device_id,status,progress) VALUES(?,?,?)',(device,status,progress))
db.commit();result=api('GET','/pattern/upload-queue?page=2&pageSize=10');summary=result['summary'];check('总进度按25项计算，不随第二页只显示10项而改变',len(result['list'])==10 and summary==dict(total=25,completed=10,failed=5,canceled=0,overallProgress=62),summary)
result=api('GET','/pattern/upload-queue?status=failed');check('处理完毕含失败也可100%，同时明确失败数量',result['summary']['overallProgress']==100 and result['summary']['completed']==0 and result['summary']['failed']==5,result['summary'])
db.execute("UPDATE upload_tasks SET status='completed',progress=100 WHERE status='uploading'");db.commit();result=api('GET','/pattern/upload-queue');check('真实任务状态改变后总进度从62更新为64',result['summary']['overallProgress']==64 and result['summary']['completed']==11,result['summary']);db.close();sys.exit(int(bool(failures)))
