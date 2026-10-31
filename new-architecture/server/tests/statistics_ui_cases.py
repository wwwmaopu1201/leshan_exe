"""Seed known business totals, test real statistics APIs, then their native client pages."""
import datetime
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import urllib.parse
import urllib.request
base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
root=Path(__file__).resolve().parents[3];log=root/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
def api(path):
    req=urllib.request.Request(base+path,headers={'Authorization':'Bearer '+token})
    with urllib.request.urlopen(req,timeout=5) as response:return json.load(response)['data']
def check(row,name,passed,detail=''):
    global failures
    item=dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt统计接口；临时SQLite已知员工/设备/生产/薪资/报警数据；原生页面另测')
    with log.open('a') as out:out.write(json.dumps(item,ensure_ascii=False)+'\n')
    print(row,item['result'],name,detail,flush=True);failures+=not passed
req=urllib.request.Request(base+'/auth/login',json.dumps(dict(username='admin',password='admin123')).encode(),{'Content-Type':'application/json'})
with urllib.request.urlopen(req) as response:token=json.load(response)['data']['token']
day=datetime.date.today().isoformat();db=sqlite3.connect(sys.argv[3])
for identifier,name in ((51,'车间甲'),(52,'车间乙')):db.execute('INSERT INTO groups(id,name,parent_id) VALUES(?,?,1)',(identifier,name))
for identifier,name,group in ((61,'哈哈哈',51),(62,'设备乙',52)):db.execute('INSERT INTO devices(id,code,name,status,group_id) VALUES(?,?,?,\'idle\',?)',(identifier,str(identifier),name,group))
db.execute("UPDATE devices SET current_speed=2000,needle_count_value=12345,current_pattern_name='监控花型',status='working' WHERE id=61")
db.execute("INSERT INTO devices(id,code,name,status) VALUES(63,'63','未分组设备丙','idle')")
for identifier,code,name,group in ((71,'0001001','员工甲',51),(72,'0001002','员工乙',52)):db.execute('INSERT INTO employees(id,code,name,phone,group_id) VALUES(?,?,?,?,?)',(identifier,code,name,'13800138000' if identifier==71 else '13900138000',group))
db.execute("INSERT INTO patterns(id,name,file_name,file_path,unit_price,stitches) VALUES(81,'统计花型','test-only.dst','test-only.dst',2,100)")
for identifier,employee,device,pieces,hours in ((91,71,61,5,.5),(92,72,62,10,1),(93,71,63,0,0)):
 db.execute('INSERT INTO salary_records(id,employee_id,device_id,pattern_id,pattern_name,pieces,unit_price,salary,total_amount,record_date) VALUES(?,?,?,?,?,?,?,?,?,?)',(identifier,employee,device,81,'统计花型',pieces,2,pieces*2,pieces*2,day))
 db.execute('INSERT INTO production_records(id,employee_id,device_id,pattern_id,pattern_name,pieces,stitches,running_time,idle_time,record_date,start_time,end_time) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)',(identifier,employee,device,81,'统计花型',pieces,pieces*100,hours,0,day,day+' 08:00:00',day+(' 08:00:00' if pieces==0 else ' 08:30:00' if device==61 else ' 09:00:00')))
for identifier,device,duration,status,code in ((111,61,60,'resolved','E.025'),(112,61,120,'pending','E.026'),(113,62,180,'resolved','E.027')):
 db.execute('INSERT INTO alarm_records(id,device_id,alarm_code,alarm_type,description,duration,status,start_time) VALUES(?,?,?,?,?,?,?,?)',(identifier,device,code,'报警'+code,'报警'+code,duration,status,day+' 10:00:00'))
db.commit();db.close();query='?startDate='+day+'&endDate='+day
process=api('/statistics/process'+query)['list'];known=next(x for x in process if x['deviceId']==61)
check(289,'平均缝纫速度=500针/30分钟，单位针/分钟而非实时转速',abs(known['sewSpeed']-16.67)<.001,known['sewSpeed'])
salary=api('/statistics/salary'+query)
check(281,'已知两员工15件、工价2元汇总薪资30元',salary['totalSalary']==30 and salary['totalPieces']==15 and salary['averageSalary']==15,salary['summary'])
filtered=api('/statistics/salary'+query+'&deviceIds=61')
check(281,'限定单设备后只统计5件和10元',filtered['totalSalary']==10 and filtered['totalPieces']==5)
reversed_name=urllib.parse.quote('哈哈哈哈')
check(277,'工资统计接口设备名也采用双向包含',api('/statistics/salary'+query+'&deviceName='+reversed_name)['total']==1)
check(96,'设备name专用筛选参数也采用双向包含',api('/device/list?name='+reversed_name)['total']==1)
future=(datetime.date.today()+datetime.timedelta(days=30)).isoformat();empty=api('/statistics/salary?startDate='+future+'&endDate='+future)
check(279,'接口明确搜索未来日期不混入历史工资',empty['total']==0 and empty['totalSalary']==0)
for endpoint,row in [('process',288),('duration',294)]:
 data=api('/statistics/'+endpoint+query+'&deviceIds=61')
 check(row,endpoint+'已知单设备统计可返回非空明细',data['total']>0,data.get('summary'))
 if endpoint=='process':check(287,'加工概况按设备筛选后上方总产量为5而非全局15',data.get('summary',{}).get('totalPieces',data.get('totalPieces'))==5,data.get('summary'))
alarm=api('/statistics/alarm'+query)
check(298,'三次报警60+120+180秒合计6分钟',alarm['totalAlarms']==3 and alarm['totalDuration']==6,alarm['summary'])
check(299,'已处理率为已处理2次除总3次，四舍五入66.67%',alarm['resolvedRate']==66.67)
check(304,'日均报警时长360秒除3次为2分钟',alarm['alarmTrend'][0]['avgDuration']==2)
filtered=api('/statistics/alarm'+query+'&deviceIds=61')
check(298,'筛选单设备后仅两次报警合计3分钟',filtered['totalAlarms']==2 and filtered['totalDuration']==3)
check(299,'单设备已处理率1/2为50%',filtered['resolvedRate']==50)
check(304,'筛选后日均报警90秒为1.5分钟',filtered['alarmTrend'][0]['avgDuration']==1.5)
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
result=subprocess.run([client,'--statistics-regression-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOER_STATS_DAY=day),timeout=60)
if result.returncode==0:
 import warnings
 from openpyxl import load_workbook
 for mode in ('all','merged'):
  path=root/'docs/qt-customer-regression-2026-10-23'/('supplement-history-'+mode+'.xlsx')
  with warnings.catch_warnings(record=True) as emitted:
   warnings.simplefilter('always');book=load_workbook(path,data_only=True)
  check(325,'补测通用XLSX导出器包含默认样式，实际读取无缺省样式警告：'+mode,not any('default style' in str(x.message) for x in emitted))
  sheet=book.active;values=list(sheet.values);headers=values[0];items=[dict(zip(headers,row)) for row in values[1:]]
  check(322,'补测实际工资导出文件保留删除员工工号前导零及历史总额：'+mode,len(items)==3 and any(x['员工工号']=='0001001' and x['员工姓名']=='员工甲' for x in items) and sum(float(x['加工件数']) for x in items)==15 and sum(float(x['工资(元)']) for x in items)==30)
  check(322,'补测工资导出的金额和件数为Excel数值类型：'+mode,all(isinstance(x['工资(元)'],(int,float)) and isinstance(x['加工件数'],(int,float)) for x in items))
  check(322,'补测工资导出合计金额为数值，可由Excel求和：'+mode,all(isinstance(x['合计(元)'],(int,float)) for x in items) and sum(x['合计(元)'] for x in items)==30)
  book.close()
sys.exit(1 if failures or result.returncode else 0)
