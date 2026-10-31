"""Observe real minute timer ticks: seven tables, disabled tick, re-enabled delta."""
import datetime as dt
import os
import sqlite3
import subprocess
import sys
import time
from supplement_helpers import CustomerRun, wait_for

r=CustomerRun(sys.argv[1], '补测：真实Qt服务端60秒自动同步定时器+本次独占MySQL8.4；无手动同步请求、无加速测试时钟')
def mysql(sql):
    result=subprocess.run(['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uroot',
                           '--batch','--raw','--skip-column-names','boer_timer','--execute',sql],
                          env=dict(os.environ,MYSQL_PWD='BoerQt_TestOnly_2026'),capture_output=True,text=True,timeout=8)
    return result.returncode,result.stdout.strip(),result.stderr

assert wait_for(lambda:mysql('SELECT 1')[0]==0,30),'test MySQL unavailable'
config=dict(dbType='mysql',host='127.0.0.1',port=18306,username='root',password='BoerQt_TestOnly_2026',
            database='boer_timer',enabled=True,syncIntervalMinutes=1)
assert r.data('POST','/system/database/test',config)['writable']
device=r.data('POST','/device',dict(code='TIMER-1',name='定时初始设备'))['id']
employee=r.data('POST','/employee',dict(code='0088',name='定时员工',phone='13900138998',groupId=1))['id']
db=sqlite3.connect(sys.argv[3]);today=dt.date.today().isoformat()
pattern=db.execute("INSERT INTO patterns(name,file_name,pattern_type,order_no) VALUES('定时花型','timer.NTP','测试','TIMER001')").lastrowid
db.execute("INSERT INTO production_records(device_id,employee_id,pattern_id,pattern_name,pieces,record_date) VALUES(?,?,?,'定时花型',2,?)",(device,employee,pattern,today))
db.execute("INSERT INTO salary_records(device_id,employee_id,pattern_id,pattern_name,pieces,salary,total_amount,record_date) VALUES(?,?,?,'定时花型',2,5,5,?)",(device,employee,pattern,today))
db.execute("INSERT INTO alarm_records(device_id,alarm_code,description,start_time,duration,status) VALUES(?,'E.059','先回原点!',?,60,'resolved')",(device,today+' 08:00:00'));db.commit()
r.data('POST','/system/database/config',config)
before=r.data('GET','/system/database/sync-status')
r.check(393,'补测保存开启配置后显示等待首次自动同步',before['status']=='waiting_first_sync',before)
print('Waiting for real first 60-second timer tick',flush=True)
def synced():
    status=r.data('GET','/system/database/sync-status')
    return status if status['lastSyncAt']>0 else None
first=wait_for(synced,75)
r.check(393,'补测无需点击立即同步，实际60秒定时器完成首次同步',bool(first),first)
if not first:sys.exit(1)
tables=['groups','devices','employees','patterns','production_records','alarm_records','salary_records']
for table in tables:
    expected=db.execute('SELECT COUNT(*) FROM `'+table+'`').fetchone()[0]
    code,count,error=mysql('SELECT COUNT(*) FROM `'+table+'`')
    r.check(388 if table in ('devices','employees','patterns','production_records') else 389,
            '补测首次真实定时同步表'+table+'完整行数',code==0 and count==str(expected),dict(expected=expected,actual=count,error=error))
first_at=first['lastSyncAt'];r.data('POST','/system/database/config',dict(config,enabled=False))
r.data('PUT','/device/'+str(device),dict(name='定时恢复后更新😀'))
print('Waiting across disabled timer tick (65 seconds)',flush=True)
time.sleep(65)
unchanged=r.data('GET','/system/database/sync-status');code,name,error=mysql('SELECT name FROM devices WHERE id='+str(device))
r.check(376,'补测关闭自动同步跨过完整定时周期，目标不变且水位不前进',unchanged['status']=='disabled' and unchanged['lastSyncAt']==first_at and name=='定时初始设备',dict(status=unchanged,target=name))
r.data('POST','/system/database/config',config)
print('Waiting for real re-enabled timer tick',flush=True)
def advanced():
    status=r.data('GET','/system/database/sync-status')
    return status if status['lastSyncAt']>first_at else None
last=wait_for(advanced,75);code,name,error=mysql('SELECT name FROM devices WHERE id='+str(device))
r.check(393,'补测重新开启后下一实际定时周期执行增量更新',bool(last) and code==0 and name=='定时恢复后更新😀',dict(status=last,target=name,error=error))
r.check(394,'补测定时增量同步中文及emoji不乱码',name=='定时恢复后更新😀',name)
r.data('POST','/system/database/config',dict(config,enabled=False));db.close()
sys.exit(int(bool(r.failures)))
