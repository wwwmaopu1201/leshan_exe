"""Isolated customer regression baseline, with per-assertion evidence."""
import json
import os
from pathlib import Path
import subprocess
import socket
import sys
import time
import urllib.request

root = Path(__file__).resolve().parents[3]
report = root / 'docs/qt-customer-regression-2026-10-23'
report.mkdir(parents=True, exist_ok=True)
server = '/private/tmp/boer-qt-customer/server/boer-qt-server.app/Contents/MacOS/boer-qt-server'
client = '/private/tmp/boer-qt-customer/client/boer-qt-client.app/Contents/MacOS/boer-qt-client'
env = dict(os.environ, BOERLAN_SKIP_TRIAL='1', QT_QPA_PLATFORM='offscreen')
cases = ['customer_cases.py', 'overview_cases.py', 'api_smoke.py', 'tcp_smoke.py', 'device_protocol_smoke.py',
         'batch_transfer_cases.py', 'client-integration']
if len(sys.argv) > 1:
    cases = sys.argv[1:]
failed_suites = 0
for index, case in enumerate(cases):
    directory = Path('/private/tmp/boer-qt-customer') / ('run-' + str(time.time_ns()))
    directory.mkdir()
    reservations=[]
    for _ in range(2):
        reserved=socket.socket();reserved.bind(('127.0.0.1',0));reservations.append(reserved)
    port,tcp=[reserved.getsockname()[1] for reserved in reservations]
    for reserved in reservations:reserved.close()
    suite_env=dict(env)
    if case.startswith(('external_', 'primary-mysql')):
        suite_env['QT_PLUGIN_PATH']='/private/tmp/boer-qt-customer/sql-driver-ninja/share/qt/plugins'
        suite_env['ODBCSYSINI']='/private/tmp/boer-qt-customer/odbc'
    if case.startswith('primary-mysql'):
        subprocess.run(['mysql','--protocol=TCP','-h127.0.0.1','-P18306','-uboer_qt_sync',
                        '--execute','DROP DATABASE IF EXISTS boer_primary; CREATE DATABASE boer_primary CHARACTER SET utf8mb4'],
                       env=dict(suite_env,MYSQL_PWD='QtSync_TestOnly_2026'),check=True)
        suite_env.update(DB_TYPE='mysql',DB_HOST='127.0.0.1',DB_PORT='18306',DB_USER='boer_qt_sync',
                         DB_PASSWORD='QtSync_TestOnly_2026',DB_NAME='boer_primary')
    with (report / (case + '-server.log')).open('w') as log:
        proc = subprocess.Popen([server, '--headless', '--port', str(port), '--tcp-port', str(tcp),
                                 '--db', str(directory / 'test.db'), '--data-dir', str(directory)],
                                env=suite_env, stdout=log, stderr=subprocess.STDOUT)
        try:
            for attempt in range(100):
                if proc.poll() is not None:raise RuntimeError('server exited: '+str(proc.returncode))
                try:
                    urllib.request.urlopen(f'http://127.0.0.1:{port}/api/healthz', timeout=1).close()
                    break
                except Exception:
                    if proc.poll() is not None:
                        raise RuntimeError('server exited: ' + str(proc.returncode))
                    time.sleep(.1)
            else:
                raise RuntimeError('server readiness timeout')
            if case == 'client-integration':
                command = [client, '--api-self-test', '--server-host', '127.0.0.1', '--server-port', str(port)]
            elif case == 'client-catalog-regression':
                command = [client, '--catalog-regression-test', '--server-host', '127.0.0.1', '--server-port', str(port)]
            else:
                actual_case={'primary-mysql-queue-clock':'queue_clock_cases.py','primary-mysql-large-devices':'large_device_consistency_cases.py','primary-mysql-confirmed-rules':'confirmed_business_rules_cases.py','primary-mysql-api-smoke':'api_smoke.py','primary-mysql-catalog-batch':'catalog_batch_cases.py'}.get(case,case)
                command = [sys.executable, str(Path(__file__).with_name('recorded_run.py')),
                           str(report / 'assertions.jsonl'), str(Path(__file__).with_name(actual_case)), str(port), str(tcp)]
                if actual_case in ('device_protocol_smoke.py', 'overview_cases.py','device_upload_cases.py','employee_cases.py','external_mysql_cases.py','statistics_ui_cases.py','catalog_batch_cases.py','production_lifecycle_cases.py','employee_protocol_cases.py','alarm_display_cases.py','upload_summary_cases.py','statistics_boundary_cases.py','external_timer_cases.py','large_device_consistency_cases.py','storage_failure_cases.py','concurrency_soak_cases.py','queue_clock_cases.py','http_abort_cases.py','disk_full_cases.py','pattern_ui_cases.py'):
                    command.append(str(directory / 'test.db'))
                if actual_case=='concurrency_soak_cases.py':command.append(str(proc.pid))
            with (report / (case + '.log')).open('w') as out:
                result = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT,
                                        env=dict(suite_env,BOER_CUSTOMER_ROW_LOG=str(report / 'row-results.jsonl')), timeout=420 if case=='concurrency_soak_cases.py' else 240)
            with (report / 'suites.jsonl').open('a') as out:
                out.write(json.dumps(dict(case=case, exit=result.returncode, database=str(directory / 'test.db'),httpPort=port,tcpPort=tcp)) + '\n')
            print(case, result.returncode, flush=True)
            failed_suites += result.returncode != 0
        except Exception as error:
            with (report / 'suites.jsonl').open('a') as out:
                out.write(json.dumps(dict(case=case,exit=99,database=str(directory/'test.db'),httpPort=port,tcpPort=tcp,error=repr(error)))+'\n')
            print(case,'ERROR',repr(error),flush=True)
            failed_suites+=1
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=5)
sys.exit(1 if failed_suites else 0)
