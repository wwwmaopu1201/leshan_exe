"""Four real Qt application processes and separate authenticated users."""
import os
import json
from pathlib import Path
import subprocess
import sys
from supplement_helpers import CustomerRun
r=CustomerRun(sys.argv[1],'非Windows补测：4个真实独立Qt客户端进程/账号并发操作页面；临时SQLite和本机offscreen')
for i in range(40):r.data('POST','/device',dict(code=f'MULTI{i}',name=f'多端设备{i}',groupId=1))
for i in range(4):r.data('POST','/user',dict(username=f'multi{i}',nickname=f'多端用户{i}',password='test1234',phone=f'1380013800{i}',role='user',groupId=1))
folder=Path(__file__).resolve().parents[3]/'docs/qt-customer-regression-2026-10-23/nonwindows-reliability-2026-10-24';folder.mkdir(exist_ok=True)
client='/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
processes=[];logs=[]
try:
 for i in range(4):
  log=(folder/f'multi-client-{i}.log').open('w');logs.append(log)
  marker=folder/f'multi-client-{i}-complete.json';marker.unlink(missing_ok=True)
  proc=subprocess.Popen([client,'--ui-self-test','--server-host','127.0.0.1','--server-port',sys.argv[1]],env=dict(os.environ,BOER_TEST_MULTI_CLIENT='1',BOER_TEST_MULTI_USER=f'multi{i}',BOER_TEST_MULTI_MARKER=str(marker)),stdout=log,stderr=subprocess.STDOUT);processes.append(proc)
 results=[proc.wait(timeout=100) for proc in processes]
 r.check(345,'4个真实Qt客户端进程同时轮换页面40秒均退出成功',results==[0]*4 and all((folder/f'multi-client-{i}-complete.json').exists() and json.loads((folder/f'multi-client-{i}-complete.json').read_text()).get('passed') is True for i in range(4)),results)
 r.check(482,'多客户端结束后服务与40台设备数据保持完整',len(r.data('GET','/device/list?fullModel=1'))==40 and r.call('GET','/healthz')[0]==200)
finally:
 for proc in processes:
  if proc.poll() is None:proc.kill();proc.wait(timeout=10)
 for log in logs:log.close()
sys.exit(int(bool(r.failures)))
