"""Authenticate, close, and restart the actual Qt GUI with the same isolated preferences."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import sys
root=Path(__file__).resolve().parents[3];report=root/'docs/qt-customer-regression-2026-10-23'
test=Path(tempfile.mkdtemp(prefix='login-reopen-',dir='/private/tmp/boer-qt-customer'));settings=test/'settings';settings.mkdir()
server='/private/tmp/boer-qt-customer/server/boer-qt-server.app/Contents/MacOS/boer-qt-server'
print('Isolated test directory:',test,flush=True)
def freeports():
 sockets=[socket.socket(),socket.socket()]
 for value in sockets:value.bind(('127.0.0.1',0))
 ports=[value.getsockname()[1] for value in sockets]
 for value in sockets:value.close()
 return ports
failed=False
for stage in ('first','second'):
 port,tcp=freeports();env=dict(os.environ,QT_QPA_PLATFORM='offscreen',BOERLAN_SKIP_TRIAL='1',BOER_TEST_SETTINGS_DIR=str(settings),BOER_TEST_LOGIN_REOPEN=stage,BOER_TEST_LOGIN_ARTIFACT_DIR=str(report),BOER_CUSTOMER_ROW_LOG=str(report/'row-results.jsonl'))
 with (report/f'server-login-reopen-{stage}.log').open('w') as log:
  result=subprocess.run([server,'--db',str(test/'app.db'),'--data-dir',str(test),'--port',str(port),'--tcp-port',str(tcp)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=30)
 print(stage,result.returncode,flush=True);failed|=result.returncode!=0
 if failed:break
with (report/'suites.jsonl').open('a') as out:out.write(json.dumps(dict(case='server-login-reopen',exit=int(failed),database=str(test/'app.db'),settings=str(settings)))+'\n')
sys.exit(int(failed))
