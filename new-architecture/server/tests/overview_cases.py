"""Customer rows for home/dashboard, using a known disposable SQLite fixture."""
import datetime
import json
import math
from pathlib import Path
import sqlite3
import sys
import time
import urllib.request

base = f'http://127.0.0.1:{sys.argv[1]}/api'
token = ''
failed = 0
log = Path(__file__).resolve().parents[3] / 'docs/qt-customer-regression-2026-10-23/row-results.jsonl'


def api(path, body=None):
    request = urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
                                     {'Content-Type':'application/json','Authorization':'Bearer '+token})
    return json.load(urllib.request.urlopen(request,timeout=10))['data']


def check(row, name, test):
    global failed
    try:
        assert test()
        result, detail = 'PASS', ''
    except Exception as error:
        result, detail = 'FAIL', repr(error)
        failed += 1
    with log.open('a') as out:
        out.write(json.dumps(dict(row=row,name=name,result=result,detail=detail,
                                  scope='Qt服务端真实HTTP接口；独立SQLite已知数据；非真实设备'),ensure_ascii=False)+'\n')
    print(row,result,name,detail,flush=True)


token = api('/auth/login',dict(username='admin',password='admin123'))['token']
a = api('/device',dict(code='HOME-A',name='运行设备',status='offline'))['id']
b = api('/device',dict(code='HOME-B',name='待机设备',status='offline'))['id']
db = sqlite3.connect(sys.argv[3])
local_now=datetime.datetime.now().astimezone()
today=local_now.date();day=today.isoformat();previous=(today-datetime.timedelta(days=1)).isoformat()
midnight=local_now.replace(hour=0,minute=0,second=0,microsecond=0)
seconds=min(3600,max(1,int((local_now-midnight).total_seconds())-60))
hours=seconds/3600
def utc_text(value):return value.astimezone(datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S')
for device in (a,b):
    started,ended=utc_text(midnight),utc_text(midnight+datetime.timedelta(seconds=seconds))
    db.execute('INSERT INTO device_runtime_sessions(device_id,started_at,ended_at,last_seen_at,duration_seconds) VALUES(?,?,?,?,?)',
               (device,started,ended,ended,seconds))
db.execute('INSERT INTO device_runtime_sessions(device_id,started_at,ended_at,duration_seconds) VALUES(?,?,?,18000)',
           (b,utc_text(midnight-datetime.timedelta(days=1)),utc_text(midnight-datetime.timedelta(days=1)+datetime.timedelta(hours=5))))
db.execute('INSERT INTO production_records(device_id,pattern_name,pieces,running_time,record_date) VALUES(?,?,?,?,?)',
           (a,'花型A',2,hours/2,day+' 00:00:00'))
db.execute('INSERT INTO production_records(device_id,pattern_name,pieces,running_time,record_date) VALUES(?,?,?,?,?)',
           (b,'昨日花型',90,4,previous+' 04:00:00'))
db.commit()
home = api('/statistics/home')
usage = {x['deviceId']:x for x in home['deviceUsage']}
check(19,'待机设备加工时长为0，待机等于已知会话时长',lambda: usage[b]['processingTime']==0 and math.isclose(usage[b]['idleTime'],hours,abs_tol=1e-6))
check(20,'按UTC存储的已知会话正确换算为本地当日小时数',lambda: math.isclose(usage[b]['runningTime'],hours,abs_tol=1e-6))
check(23,'首页设备使用率仅统计今天，不混入昨日4小时加工',lambda: usage[b]['efficiency']==0 and usage[a]['efficiency']==50)
if local_now.hour>0:
    check(34,'本地00时两设备在线，UTC前一日记录不应被漏算',lambda: home['runningStatusByHour'][0]['online']==2)
check(36,'当前在线/离线数量与设备状态一致',lambda: home['onlineDevices']==0 and home['offlineDevices']==2 and home['runningStatusByHour'][-1]['online']==0)
time.sleep(.15)
later = api('/statistics/home')
check(22,'已结束的离线会话不会继续累计待机时间',lambda: math.isclose(next(x for x in later['deviceUsage'] if x['deviceId']==b)['idleTime'],hours,abs_tol=1e-6))
dashboard = api('/statistics/dashboard?deviceId='+str(a))
check(54,'筛选设备后开机数与设备状态一致',lambda: dashboard['scopeDeviceCount']==1 and dashboard['onlineDeviceCount']==0)
check(21,'筛选单设备后时长仅包含其已知会话和加工时长',lambda: math.isclose(dashboard['runningTime'],hours,abs_tol=1e-6) and math.isclose(dashboard['processingTime'],hours/2,abs_tol=1e-6))
check(57,'筛选单设备使用率=加工时长/开机时长=50%',lambda: math.isclose(dashboard['utilizationRate'],50,abs_tol=1e-6))
check(56,'看板三张趋势图时间轴均为近7天',lambda: len(dashboard['hourlyProduction'])==7 and
      [x['date'] for x in dashboard['hourlyProduction']]==[x['date'] for x in dashboard['runningProcessingTrend']]==[x['date'] for x in dashboard['utilizationTrend']])
def trend(start,end):
    return api('/statistics/production-trend?startDate='+start+'&endDate='+end)['productionTrend']
future = trend('2030-08-01','2030-08-30')
check(41,'未来日期只返回零产量，不混入历史生产',lambda: len(future)==30 and all(x['pieces']==0 for x in future))
month = trend((today-datetime.timedelta(days=29)).isoformat(),day)
check(43,'30天趋势保留无产量日期并填零',lambda: len(month)==30 and sum(x['pieces']==0 for x in month)==28)
check(47,'近一月日期连续且完整',lambda: [x['date'] for x in month]==[(today-datetime.timedelta(days=i)).isoformat() for i in range(29,-1,-1)])
check(46,'开始结束日期颠倒自动互换',lambda: trend(day,previous)==trend(previous,day))
cross = trend('2025-12-30','2026-01-02')
check(42,'跨年产量接口保留完整年份日期',lambda: [x['date'] for x in cross]==['2025-12-30','2025-12-31','2026-01-01','2026-01-02'])
check(48,'未登录员工的设备生产记录仍计入产量',lambda: month[-1]['pieces']==2)
db.close()
sys.exit(1 if failed else 0)
