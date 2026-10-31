"""Run both real Qt windows with isolated settings and multiple Qt scale factors."""
import json
import os
from pathlib import Path
import subprocess
import sys
import socket
import tempfile

root=Path(__file__).resolve().parents[3]
report=root/'docs/qt-customer-regression-2026-10-23'
artifacts=report/'window-compatibility-2026-10-24'
platform=sys.argv[1] if len(sys.argv)>1 else 'offscreen'
scales=sys.argv[2:] or ['1','1.25','1.5','2']
failures=0
for scale in scales:
    for side in ('client','server'):
        case=f'window-{side}-{platform}-{scale}'
        folder=artifacts/f'{side}-{platform}-{scale}';folder.mkdir(parents=True,exist_ok=True)
        exe=f'/private/tmp/boer-qt-customer/{side}/boer-qt-{side}.app/Contents/MacOS/boer-qt-{side}'
        env=dict(os.environ,QT_QPA_PLATFORM=platform,QT_SCALE_FACTOR=scale,BOER_TEST_WINDOW_COMPAT='1',
                 BOER_CUSTOMER_ROW_LOG=str(report/'row-results.jsonl'),BOER_WINDOW_ARTIFACT_DIR=str(folder),BOERLAN_SKIP_TRIAL='1')
        with (artifacts/(case+'.log')).open('w') as out:
            try:
                result=subprocess.run([exe,'--ui-self-test' if side=='client' else '--customer-ui-regression'],
                                      env=env,stdout=out,stderr=subprocess.STDOUT,timeout=180)
                code=result.returncode
            except subprocess.TimeoutExpired:
                out.write('\nTIMEOUT\n');code=99
        with (report/'suites.jsonl').open('a') as out:
            out.write(json.dumps(dict(case=case,exit=code,log=str(artifacts/(case+'.log'))))+'\n')
        print(case,code,flush=True);failures+=code!=0
        if side=='server':
            with tempfile.TemporaryDirectory(prefix='window-login-',dir='/private/tmp/boer-qt-customer') as directory:
                sockets=[socket.socket(),socket.socket()]
                for reserved in sockets:reserved.bind(('127.0.0.1',0))
                ports=[reserved.getsockname()[1] for reserved in sockets]
                for reserved in sockets:reserved.close()
                case=f'window-server-login-{platform}-{scale}'
                login_env=dict(env,BOER_TEST_LOGIN_WINDOW_COMPAT='1',BOER_TEST_SETTINGS_DIR=directory+'/settings')
                login_env.pop('BOER_TEST_WINDOW_COMPAT',None)
                with (artifacts/(case+'.log')).open('w') as out:
                    try:
                        result=subprocess.run([exe,'--db',directory+'/login.db','--data-dir',directory,
                                               '--port',str(ports[0]),'--tcp-port',str(ports[1])],
                                              env=login_env,stdout=out,stderr=subprocess.STDOUT,timeout=40)
                        code=result.returncode
                    except subprocess.TimeoutExpired:out.write('\nTIMEOUT\n');code=99
                with (report/'suites.jsonl').open('a') as out:out.write(json.dumps(dict(case=case,exit=code,log=str(artifacts/(case+'.log'))))+'\n')
                print(case,code,flush=True);failures+=code!=0
sys.exit(int(bool(failures)))
