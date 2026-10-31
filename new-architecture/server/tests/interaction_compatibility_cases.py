"""Isolated client/server interaction and restore cases on offscreen or Cocoa."""
import json
import os
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[3]
report=root/'docs/qt-customer-regression-2026-10-23'
artifacts=report/'interaction-compatibility-2026-10-24'
platform=sys.argv[1] if len(sys.argv)>1 else 'offscreen'
scales=sys.argv[2:] or ['1','2']
failures=0
for scale in scales:
    for side in ('client','server'):
        case=f'interaction-{side}-{platform}-{scale}'
        folder=artifacts/f'{side}-{platform}-{scale}';folder.mkdir(parents=True,exist_ok=True)
        command=f'/private/tmp/boer-qt-customer/{side}/boer-qt-{side}.app/Contents/MacOS/boer-qt-{side}'
        env=dict(os.environ,QT_QPA_PLATFORM=platform,QT_SCALE_FACTOR=scale,BOER_TEST_INTERACTION_COMPAT='1',
                 BOERLAN_SKIP_TRIAL='1',BOER_CUSTOMER_ROW_LOG=str(report/'row-results.jsonl'),BOER_INTERACTION_ARTIFACT_DIR=str(folder))
        with (artifacts/(case+'.log')).open('w') as out:
            try:
                result=subprocess.run([command,'--ui-self-test' if side=='client' else '--customer-ui-regression'],
                                      env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120)
                code=result.returncode
            except subprocess.TimeoutExpired:out.write('\nTIMEOUT\n');code=99
        with (report/'suites.jsonl').open('a') as out:out.write(json.dumps(dict(case=case,exit=code,log=str(artifacts/(case+'.log'))))+'\n')
        print(case,code,flush=True);failures+=code!=0
sys.exit(int(bool(failures)))
