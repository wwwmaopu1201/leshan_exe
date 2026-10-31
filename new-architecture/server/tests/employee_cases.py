"""Employee customer rows: validation, overwrite import and historical retention."""
import datetime
import json
from pathlib import Path
import sqlite3
import sys
import urllib.error
import urllib.request

base=f'http://127.0.0.1:{sys.argv[1]}/api';token='';failures=0
log=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/row-results.jsonl'


def call(method,path,body=None):
    request=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,
        {'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
    try:
        with urllib.request.urlopen(request,timeout=10) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)


def data(method,path,body=None):return call(method,path,body)[1]['data']


def check(row,name,operation,expect):
    global failures
    try:actual=operation();passed=expect(actual)
    except Exception as error:actual,passed=repr(error),False
    with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',actual=actual,
        scope='真实Qt服务端员工/导入/统计接口；独立临时SQLite库，CSV界面另测'),ensure_ascii=False)+'\n')
    print(row,'PASS' if passed else 'FAIL',name,flush=True);failures+=not passed


token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
for index,code in enumerate(('E100','工号甲','12!','','123456789012')):
    check(311,'工号拒绝非1至11位数字：'+repr(code),lambda:call('POST','/employee',dict(code=code,name='测试',phone='13800138'+str(100+index),groupId=1)),
          lambda x:x[0]==400 and '工号' in x[1]['message'])
employee=data('POST','/employee',dict(code='0001001',name='旧员工',phone='138 0013 8000',groupId=1,remark='原备注'))
identifier=employee['id']
check(312,'手机号包含空格时规范化为11位号码',lambda:call('GET','/employee/'+str(identifier)),lambda x:x[1]['data']['phone']=='13800138000')
check(311,'数字工号保留前导零',lambda:call('GET','/employee/'+str(identifier)),lambda x:x[1]['data']['code']=='0001001')
for name in ('','超'*9):
    check(320,'员工姓名为空或超8字拒绝：'+str(len(name)),lambda:call('POST','/employee',dict(code='1008',name=name,phone='13700138000',groupId=1)),lambda x:x[0]==400)
body=dict(groupId=1,employees=[dict(code='0001001',name='新员工',phone='139 0013 8000')])
check(318,'未选择覆盖时报告重复工号而不悄悄修改',lambda:call('POST','/employee/import',body),lambda x:x[0]==200 and x[1]['data']['successCount']==0 and len(x[1]['data']['errors'])==1)
check(318,'明确选择覆盖后更新同工号员工并保留ID',lambda:(call('POST','/employee/import',dict(body,overwriteExisting=True)),call('GET','/employee/'+str(identifier))),
      lambda x:x[0][1]['data']['successCount']==1 and x[1][1]['data']['id']==identifier and x[1][1]['data']['name']=='新员工' and x[1][1]['data']['phone']=='13900138000')
check(318,'覆盖未提供备注列时保留原备注',lambda:call('GET','/employee/'+str(identifier)),lambda x:x[1]['data']['remark']=='原备注')
check(320,'批量导入同样拒绝超长姓名',lambda:call('POST','/employee/import',dict(groupId=1,employees=[dict(code='1010',name='名'*9,phone='13600138000')])),
      lambda x:x[1]['data']['successCount']==0 and len(x[1]['data']['errors'])==1)
check(317,'导入接口区分成功行和错误行',lambda:call('POST','/employee/import',dict(groupId=1,employees=[dict(code='1011',name='合法',phone='13500138000'),dict(code='1012',name='错误',phone='bad')])),
      lambda x:x[1]['data']['successCount']==1 and len(x[1]['data']['errors'])==1)
call('DELETE','/employee/'+str(identifier))
check(319,'删除后重新导入恢复员工而非唯一键报错',lambda:call('POST','/employee/import',body),lambda x:x[1]['data']['successCount']==1)
check(319,'重新导入保留同一员工ID和前导零工号',lambda:call('GET','/employee/'+str(identifier)),lambda x:x[0]==200 and x[1]['data']['code']=='0001001')
device=data('POST','/device',dict(code='history-device',name='历史设备'))['id']
db=sqlite3.connect(sys.argv[3]);day=datetime.date.today().isoformat()
db.execute('INSERT INTO salary_records(employee_id,device_id,pattern_name,pieces,unit_price,salary,total_amount,record_date) VALUES(?,?,?,?,?,?,?,?)',
           (identifier,device,'历史花型',2,2.5,5,5,day));db.commit()
call('DELETE','/employee/'+str(identifier))
check(322,'删除员工后生产薪资历史仍保留且可统计',lambda:call('GET','/statistics/salary'),
      lambda x:x[0]==200 and x[1]['data']['totalSalary']==5 and x[1]['data']['totalPieces']==2)
check(322,'软删除不删除员工ID关联的薪资记录',lambda:db.execute('SELECT COUNT(*) FROM salary_records WHERE employee_id=?',(identifier,)).fetchone()[0],lambda x:x==1)
export=data('GET','/employee/export')
check(325,'导出接口创建时间有真实值而非井号占位',lambda:export,lambda rows:bool(rows) and all(len(str(x.get('createdAt','')))>=10 and '####' not in str(x['createdAt']) for x in rows))
db.close();sys.exit(1 if failures else 0)
