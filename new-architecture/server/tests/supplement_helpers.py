"""HTTP and immediate row journal helpers for the second customer pass."""
import datetime
import json
import os
from pathlib import Path
import time
import urllib.error
import urllib.request


class CustomerRun:
    def __init__(self, port, scope):
        self.base = f'http://127.0.0.1:{port}/api'
        self.scope, self.token, self.failures = scope, '', 0
        self.log = Path(os.environ['BOER_CUSTOMER_ROW_LOG'])
        self.token = self.data('POST', '/auth/login', dict(username='admin', password='admin123'))['token']

    def call(self, method, path, body=None):
        req = urllib.request.Request(self.base + path,
            json.dumps(body).encode() if body is not None else None,
            {'Content-Type': 'application/json', 'Authorization': 'Bearer ' + self.token}, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def data(self, method, path, body=None):
        status, result = self.call(method, path, body)
        if status != 200 or result.get('code') != 0:
            raise RuntimeError((status, result))
        return result.get('data')

    def check(self, row, name, passed, detail=''):
        item = dict(time=datetime.datetime.now().astimezone().isoformat(), row=row, name=name,
                    result='PASS' if passed else 'FAIL', detail=detail, scope=self.scope)
        with self.log.open('a') as out:
            out.write(json.dumps(item, ensure_ascii=False) + '\n')
            out.flush()
        print(row, item['result'], name, detail, flush=True)
        self.failures += not passed


def wait_for(test, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = test()
        if value:
            return value
        time.sleep(.08)
    return test()
