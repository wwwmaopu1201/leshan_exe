"""Catalog timestamps and atomic multi-name changes, including references and swaps."""
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
mysql=os.environ.get('DB_TYPE')=='mysql';db=None if mysql else sqlite3.connect(sys.argv[3])
def sql(text):
 if mysql:
  subprocess.run(['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uboer_qt_sync','--execute',text,'boer_primary'],env=dict(os.environ,MYSQL_PWD='QtSync_TestOnly_2026'),check=True,capture_output=True)
 else:db.execute(text);db.commit()
def call(method,path,body=None):
 req=urllib.request.Request(base+path,json.dumps(body).encode() if body is not None else None,{'Content-Type':'application/json','Authorization':'Bearer '+token},method=method)
 try:
  with urllib.request.urlopen(req,timeout=10) as response:return response.status,json.load(response)
 except urllib.error.HTTPError as error:return error.code,json.load(error)
def data(method,path,body=None):return call(method,path,body)[1]['data']
def check(row,name,passed,detail=''):
 global failures
 with log.open('a') as out:out.write(json.dumps(dict(row=row,name=name,result='PASS' if passed else 'FAIL',detail=detail,scope='真实Qt目录批量接口+关联设备/花型；'+('MySQL 8.4隔离主库' if mysql else '临时SQLite')),ensure_ascii=False)+'\n')
 print(row,'PASS' if passed else 'FAIL',name,detail,flush=True);failures+=not passed
def upload(name,key,value):
 boundary='catalog-batch-upload';fields=dict(name=name,**{key:value});body=b''
 for field,text in fields.items():body+=f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"\r\n\r\n{text}\r\n'.encode()
 body+=f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="fixture.NTP"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+b'TEST-NTP'+f'\r\n--{boundary}--\r\n'.encode()
 req=urllib.request.Request(base+'/pattern/upload',body,{'Content-Type':'multipart/form-data; boundary='+boundary,'Authorization':'Bearer '+token})
 with urllib.request.urlopen(req) as response:return json.load(response)['data']['id']
token=data('POST','/auth/login',dict(username='admin',password='admin123'))['token']
cases=[('/device/type-summary','device_type_catalogs','type','devices',135,139),('/device/electric-control-type-summary','electric_control_type_catalogs','electricControlType','devices',142,146),('/pattern/type-summary','pattern_type_catalogs','patternType','patterns',239,243),('/pattern/order-summary','order_no_catalogs','orderNo','patterns',253,256)]
for index,(route,catalog,key,table,row,time_row) in enumerate(cases):
 names=[f'Batch{index}{suffix}' for suffix in ('A','B','EXISTS')]
 identifiers=[data('POST',route,dict(value=name))['id'] for name in names]
 refs=[]
 for n,name in enumerate(names[:2]):
  refs.append(data('POST','/device',dict(code=f'catalog-ref-{index}-{n}',name=f'关联{index}{n}',**{key:name}))['id'] if table=='devices' else upload(f'引用花型{index}{n}',key,name))
 sql(f"UPDATE {catalog} SET updated_at='2020-01-02 03:04:05' WHERE id={identifiers[0]}")
 sql(f"UPDATE {table} SET updated_at='2000-01-01 00:00:00' WHERE id={refs[0]}")
 rows=data('GET',route);stamp=next(x['updateTime'] for x in rows if x['value']==names[0])
 check(time_row,route+'目录时间不被旧关联记录时间覆盖',stamp.startswith('2020-01-02'),stamp)
 changed=[names[0]+'NEW',names[1]+'NEW']
 result=call('PUT',route,dict(renames=[dict(oldValue=names[n],newValue=changed[n]) for n in range(2)]))
 check(row,route+'支持真正一次修改两个不同名称',result[0]==200,result[1])
 if result[0]!=200:continue
 ref=lambda n:data('GET',('/device/' if table=='devices' else '/pattern/')+str(refs[n]))[key]
 check(row,route+'两个关联设备或花型同步更新',all(ref(n)==changed[n] for n in range(2)))
 result=call('PUT',route,dict(renames=[dict(oldValue=changed[0],newValue='PartialMustNotSave'),dict(oldValue=changed[1],newValue=names[2])]))
 check(row,route+'任一名称冲突时整批不修改',result[0]==400 and all(ref(n)==changed[n] for n in range(2)))
 result=call('PUT',route,dict(renames=[dict(oldValue=changed[n],newValue='DuplicateNew') for n in range(2)]))
 check(row,route+'批内新名称重复也整体拒绝',result[0]==400 and all(ref(n)==changed[n] for n in range(2)))
 if not mysql:
  field={'type':'type','electricControlType':'electric_control_type','patternType':'pattern_type','orderNo':'order_no'}[key]
  sql(f"CREATE TRIGGER reject_catalog_ref BEFORE UPDATE OF {field} ON {table} BEGIN SELECT RAISE(ABORT,'INTERNAL_BATCH_TEST'); END")
  result=call('PUT',route,dict(renames=[dict(oldValue=changed[n],newValue=f'Fault{index}{n}') for n in range(2)]))
  visible=data('GET',route)
  check(row,route+'写入中途失败后回滚目录和关联字段，不残留临时名称',result[0]==400 and all(ref(n)==changed[n] for n in range(2)) and not any(x['value'].startswith('__rename_') or x['value'].startswith('Fault') for x in visible))
  sql('DROP TRIGGER reject_catalog_ref')
 result=call('PUT',route,dict(renames=[dict(oldValue=changed[0],newValue=changed[1]),dict(oldValue=changed[1],newValue=changed[0])]))
 check(row,route+'两名称互换不丢失目录ID和引用归属',result[0]==200 and ref(0)==changed[1] and ref(1)==changed[0] and [x['id'] for x in result[1]['data']['items']]==identifiers[:2])
if db:db.close()
sys.exit(1 if failures else 0)
