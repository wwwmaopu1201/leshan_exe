"""Real transfer timer: CURRENT_TIMESTAMP is UTC in SQLite and the Qt MySQL session."""
import datetime
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import urllib.request
from supplement_helpers import CustomerRun
kind='MySQL' if os.environ.get('DB_TYPE')=='mysql' else 'SQLite'
r=CustomerRun(sys.argv[1],f'时钟补测：真实Qt队列定时器+临时{kind}，UTC数据库时间，不加速时钟')
d=r.data('POST','/device',dict(code='CLOCK001',name='时钟测试设备',groupId=1))['id']
boundary='queue-clock';body=f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="clock.NTP"\r\nContent-Type: application/octet-stream\r\n\r\nCLOCK\r\n--{boundary}--\r\n'.encode()
req=urllib.request.Request(r.base+'/pattern/upload',body,{'Authorization':'Bearer '+r.token,'Content-Type':'multipart/form-data; boundary='+boundary})
with urllib.request.urlopen(req) as res:p=json.load(res)['data']['id']
r.data('POST','/pattern/download',dict(patternId=p,deviceIds=[d]))
time.sleep(2.3)
def task():return r.data('GET','/pattern/queue')['list'][0]
t=task();r.check(172,f'{kind}新建离线等待任务跨过真实定时器仍等待，不误判已过10分钟',t['status']=='waiting',t)
if t['status']=='waiting':
 a=r.call('POST',f"/pattern/queue/{t['id']}/pause",{});b=r.call('POST',f"/pattern/queue/{t['id']}/resume",{})
 r.check(172,f'{kind}新任务可正常暂停恢复且不立即失败',a[0]==200 and b[0]==200)
else:r.check(172,f'{kind}新任务可正常暂停恢复且不立即失败',False)
old=(datetime.datetime.now(datetime.timezone.utc)-datetime.timedelta(minutes=11)).strftime('%Y-%m-%d %H:%M:%S')
sql=f"UPDATE download_tasks SET status='waiting',updated_at='{old}' WHERE id={t['id']}"
if kind=='SQLite':
 db=sqlite3.connect(sys.argv[3]);db.execute(sql);db.commit();db.close()
else:subprocess.run(['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uboer_qt_sync','boer_primary','--execute',sql],env=dict(os.environ,MYSQL_PWD='QtSync_TestOnly_2026'),check=True)
time.sleep(2.3);t=task();r.check(172,f'{kind}真实超过11分钟的UTC等待任务按规则超时失败',t['status']=='failed' and '超过10分钟' in t.get('message',''),t)
sys.exit(int(bool(r.failures)))
