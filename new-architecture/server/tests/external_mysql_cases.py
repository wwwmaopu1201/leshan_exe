"""Real MySQL round trips for the seven sync tables, including incremental changes."""
import datetime
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import urllib.error
import urllib.request

base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'
config=dict(dbType='mysql',host='127.0.0.1',port=18306,username='boer_qt_sync',password='QtSync_TestOnly_2026',database='boer_sync',enabled=True,syncIntervalMinutes=1)


def api(method,path,body=None):
    request=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
        {'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    try:
        with urllib.request.urlopen(request,timeout=20) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


def mysql(sql,database='boer_sync'):
    command=['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uboer_qt_sync','--default-character-set=utf8mb4','--batch','--raw','--skip-column-names',database,'--execute',sql]
    return subprocess.run(command,env=dict(os.environ,MYSQL_PWD=config['password']),check=True,text=True,capture_output=True).stdout.strip()


def record(row,name,passed,detail=''):
    global failures
    with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,
        scope='真实Qt QMYSQL驱动+隔离MySQL 8.4容器；源库为临时SQLite'),ensure_ascii=False)+'\n')
    print(row,'PASS' if passed else 'FAIL',name,detail,flush=True);failures+=not passed


token=api('POST','/auth/login',dict(username='admin',password='admin123'))[1]['data']['token']
connection=api('POST','/system/database/test',config)[1]
record(383,'真实MySQL可读写账号能测试连接',connection.get('data',{}).get('success') is True,connection)
if not connection.get('data',{}).get('success'):sys.exit(1)
wrong=api('POST','/system/database/test',dict(config,password='incorrect'))[1]
record(383,'错误密码测试连接明确失败，无假成功',wrong['code']!=0 and wrong.get('data',{}).get('success') is False)
record(387,'实际错误密码反馈账号/密码/访问权限原因，避免原始驱动英文', '账号或密码' in wrong.get('message',''),wrong)
missing=api('POST','/system/database/test',dict(config,database='boer_missing_test_database',username='root',password='BoerQt_TestOnly_2026'))[1]
record(387,'实际不存在数据库明确提示检查库名','数据库不存在' in missing.get('message',''),missing)
refused=api('POST','/system/database/test',dict(config,port=1))[1]
record(377,'实际连接拒绝提示检查地址端口服务网络','地址、端口' in refused.get('message',''),refused)

tables=['groups','devices','employees','patterns','production_records','alarm_records','salary_records']
mysql(';'.join('DROP TABLE IF EXISTS `'+table+'`' for table in tables))
db=sqlite3.connect(sys.argv[3]);today=datetime.date.today().isoformat()
db.execute("INSERT INTO devices(id,code,name,status,group_id) VALUES(51,'SYNC-51','中文设备甲😀','offline',1)")
db.execute("INSERT INTO employees(id,code,name,phone,group_id) VALUES(52,'00052','张三','13800138000',1)")
db.execute("INSERT INTO patterns(id,name,file_name,file_path,file_size,stitches,unit_price,order_no) VALUES(53,'同步花型','样本.NTP','/临时/样本.NTP',123,72000,2.5,'ORDER53')")
db.execute('INSERT INTO production_records(id,device_id,employee_id,pattern_id,pattern_name,pieces,unit_price,running_time,record_date) VALUES(54,51,52,53,?,2,2.5,.5,?)',('同步花型',today))
db.execute('INSERT INTO alarm_records(id,device_id,alarm_code,description,start_time,duration,status) VALUES(55,51,?,?,?,?,?)',('E.025','中文报警',today+' 00:01:00',60,'resolved'))
db.execute('INSERT INTO salary_records(id,employee_id,device_id,pattern_id,pattern_name,pieces,unit_price,salary,total_amount,record_date) VALUES(56,52,51,53,?,2,2.5,5,5,?)',('同步花型',today))
for table in tables:db.execute('UPDATE `'+table+'` SET created_at=?,updated_at=?',('2000-01-01 00:00:00','2000-01-01 00:00:00'))
db.commit()
api('POST','/system/database/config',config)
full=api('POST','/system/database/sync-now',{})[1]
record(393,'真实MySQL手动同步报告成功',full['code']==0,full)
for table in tables:
    expected=db.execute('SELECT COUNT(*) FROM '+('`groups`' if table=='groups' else table)).fetchone()[0]
    try:actual=int(mysql('SELECT COUNT(*) FROM `'+table+'`'))
    except Exception as error:actual=repr(error)
    record(389 if table in ('groups','alarm_records','salary_records') else 388,'同步表'+table+'行数与源库一致',actual==expected,dict(expected=expected,actual=actual))
record(394,'设备名中文及emoji同步不乱码',mysql('SELECT name FROM devices WHERE id=51')=='中文设备甲😀')
record(394,'数字工号前导零同步后仍保留',mysql('SELECT code FROM employees WHERE id=52')=='00052')
record(389,'薪资金额与生产关联完整同步',mysql('SELECT salary FROM salary_records WHERE id=56')=='5')
api('PUT','/device/51',dict(name='增量更新设备'))
db.execute("UPDATE patterns SET deleted_at=CURRENT_TIMESTAMP,updated_at=CURRENT_TIMESTAMP WHERE id=53");db.commit()
probe='/private/tmp/boer-qt-customer/server/boer-external-sync-probe'
ran=subprocess.run([probe],input=json.dumps(dict(source=dict(type='sqlite',path=sys.argv[3]),config=config,full=False)),
                   text=True,capture_output=True,env=os.environ,timeout=20)
try:incremental=json.loads(ran.stdout)
except Exception:incremental=dict(success=False,error=ran.stdout+ran.stderr)
record(393,'实际增量同步工作器执行成功',incremental.get('success') is True,incremental)
record(393,'全量同步后发生的更新不会因UTC水位遗漏',mysql('SELECT name FROM devices WHERE id=51')=='增量更新设备')
record(393,'增量同步包含软删除标记',mysql('SELECT deleted_at IS NOT NULL FROM patterns WHERE id=53')=='1')
mysql(';'.join('DROP TABLE IF EXISTS `'+table+'`' for table in tables),'boer_sync_alt')
alternate=dict(config,database='boer_sync_alt')
switched=subprocess.run([probe],input=json.dumps(dict(source=dict(type='sqlite',path=sys.argv[3]),config=alternate,full=False)),
                        text=True,capture_output=True,env=os.environ,timeout=20)
record(393,'切换空目标库后的首次增量自动补齐旧历史数据',switched.returncode==0 and
       int(mysql('SELECT COUNT(*) FROM employees','boer_sync_alt'))==1 and int(mysql('SELECT COUNT(*) FROM `groups`','boer_sync_alt'))==1)
readonly=dict(config,username='boer_qt_read',password='QtRead_TestOnly_2026')
readtest=api('POST','/system/database/test',readonly)[1]
record(385,'只读账号可连接但应明确标记不可同步',readtest.get('data',{}).get('success') is True and readtest.get('data',{}).get('writable') is False,readtest)
api('POST','/system/database/config',readonly)
denied=api('POST','/system/database/sync-now',{})[1]
record(385,'只读账号实际同步失败且不反馈成功',denied['code']!=0,denied)
db.close();sys.exit(1 if failures else 0)
