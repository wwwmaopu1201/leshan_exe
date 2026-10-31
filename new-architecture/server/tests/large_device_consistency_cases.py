"""Real API/tree/UI model consistency beyond both 1000 and 2000 boundaries."""
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import json
from supplement_helpers import CustomerRun

kind='MySQL8.4' if os.environ.get('DB_TYPE')=='mysql' else 'SQLite'
r=CustomerRun(sys.argv[1],f'补测：2250条隔离{kind}设备，真实Qt接口与客户端；非实机并发')
other=r.data('POST','/group',dict(name='隔离根分组',parentId=None))['id']
fixtures=[(i,'EARLY0001' if i==1 else 'LOAD'+str(i),'早期设备' if i==1 else '加载设备'+str(i),'备注'*100,other if i==1 else 1,'offline') for i in range(1,2251)]
if kind=='SQLite':
 db=sqlite3.connect(sys.argv[3]);db.executemany('INSERT INTO devices(id,code,name,remark,group_id,status) VALUES(?,?,?,?,?,?)',fixtures);db.commit();db.close()
else:
 def quoted(value):return str(value) if isinstance(value,int) else "'"+value.replace("'","''")+"'"
 sql='INSERT INTO devices(id,code,name,remark,group_id,status) VALUES '+','.join('('+','.join(quoted(x) for x in row)+')' for row in fixtures)
 subprocess.run(['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uboer_qt_sync','--default-character-set=utf8mb4','boer_primary'],input=sql,text=True,check=True,env=dict(os.environ,MYSQL_PWD='QtSync_TestOnly_2026'))
def tree_ids(nodes):
 ids=set()
 for node in nodes:
  if node.get('type')=='device':ids.add(int(node['id']))
  ids.update(tree_ids(node.get('children',[])))
 return ids
first=r.data('GET','/device/list?page=1&pageSize=2000');second=r.data('GET','/device/list?page=2&pageSize=2000')
r.check(474,'大数据API总数2250与两页唯一ID完全一致',first['total']==2250 and second['total']==2250 and len({x['id'] for x in first['list']+second['list']})==2250)
ids=tree_ids(r.data('GET','/device/tree'));r.check(438,'大数据API树不截断1000条，2250设备全部存在',ids==set(range(1,2251)),dict(actual=len(ids)))
model=r.data('GET','/device/list?fullModel=1');r.check(474,'显式设备全模型包含2250条且普通分页契约不变',isinstance(model,list) and len(model)==2250 and len(first['list'])==2000 and len(second['list'])==250)
r.data('POST','/role',dict(name='largeview',permissions=json.dumps(dict(deviceInfo=True))))
r.data('POST','/user',dict(username='largeuser',password='123456',nickname='隔离查看',phone='13900138001',role='largeview',groupId=1))
admin=r.token;r.token=r.data('POST','/auth/login',dict(username='largeuser',password='123456'))['token']
scoped=tree_ids(r.data('GET','/device/tree'));r.check(430,'扩大设备树读取后仍保持原业务跨分组可见规则',len(scoped)==2250 and 1 in scoped,dict(actual=len(scoped)))
limited=r.data('GET','/device/list?fullModel=1');r.check(430,'设备全模型保持账号分组仅组织管理的既有规则',len(limited)==2250 and any(x['id']==1 for x in limited))
r.token=admin
r.data('POST','/role',dict(name='largenone',permissions=json.dumps({})))
r.data('POST','/user',dict(username='largenone',password='123456',nickname='无设备权限',phone='13900138002',role='largenone',groupId=1))
r.token=r.data('POST','/auth/login',dict(username='largenone',password='123456'))['token']
r.check(430,'无设备模块权限账号无法通过全模型或树接口读取设备',r.call('GET','/device/list?fullModel=1')[0]==403 and r.call('GET','/device/tree')[0]==403)
r.token=admin
root=Path(__file__).resolve().parents[3];folder=root/'docs/qt-customer-regression-2026-10-23/interaction-compatibility-2026-10-24/client-real-api';folder.mkdir(parents=True,exist_ok=True)
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
marker=folder/'client-large-check-complete.json';marker.unlink(missing_ok=True)
result=subprocess.run([client,'--ui-self-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],
 env=dict(os.environ,BOER_TEST_LARGE_DEVICE_API='1',BOER_INTERACTION_ARTIFACT_DIR=str(folder)),timeout=60)
r.check(474,'大数据真实客户端专项入口实际完成，非旧二进制通用自测',marker.exists() and json.loads(marker.read_text()).get('complete') is True)
sys.exit(int(bool(r.failures or result.returncode)))
