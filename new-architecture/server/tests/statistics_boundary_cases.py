"""Known leap-day/year boundaries and one-sided date range guards."""
import datetime as dt
import sqlite3
import sys
from supplement_helpers import CustomerRun

r = CustomerRun(sys.argv[1], '补测：真实Qt服务端HTTP+独立SQLite已知跨年/闰日/零产量数据；不依赖设备')
db = sqlite3.connect(sys.argv[3])
device = r.data('POST', '/device', dict(code='BOUNDARY-DATE', name='日期边界设备'))['id']
employee = r.data('POST', '/employee', dict(code='0000199', name='跨年员工', phone='13800138991',groupId=1))['id']
records = [('2024-02-28 23:59:59', 2), ('2024-02-29 00:00:00', 3),
           ('2024-03-01 00:00:00', 5), ('2024-12-31 23:59:59', 7),
           ('2025-01-01 00:00:00', 11), ('2025-01-02 00:00:00', 13)]
for stamp, pieces in records:
    db.execute('INSERT INTO production_records(device_id,employee_id,pattern_name,pieces,stitches,running_time,record_date) VALUES(?,?,?,?,?,?,?)',
               (device, employee, '边界花型', pieces, pieces*100, pieces/60, stamp))
    db.execute('INSERT INTO salary_records(device_id,employee_id,pattern_name,pieces,unit_price,salary,total_amount,record_date) VALUES(?,?,?,?,?,?,?,?)',
               (device, employee, '边界花型', pieces, .5, pieces*.5, pieces*.5, stamp))
db.commit()
def trend(start, end):
    return r.data('GET', f'/statistics/production-trend?startDate={start}&endDate={end}')['productionTrend']

leap = trend('2024-02-28', '2024-03-01')
r.check(47, '补测闰年2月28至3月1日期连续且包含29日，产量2/3/5',
        [(x['date'], x['pieces']) for x in leap] == [('2024-02-28',2),('2024-02-29',3),('2024-03-01',5)], leap)
cross = trend('2024-12-30', '2025-01-02')
r.check(42, '补测跨年四天不丢边界记录并保留无生产日0值',
        [(x['date'], x['pieces']) for x in cross] == [('2024-12-30',0),('2024-12-31',7),('2025-01-01',11),('2025-01-02',13)], cross)
r.check(43, '补测单日零产量与多日零产量均明确填零',
        trend('2024-12-30','2024-12-30') == [dict(date='2024-12-30',pieces=0)] and cross[0]['pieces']==0)
for endpoint, row in [('salary',281),('process',288),('duration',294)]:
    data = r.data('GET',f'/statistics/{endpoint}?startDate=2024-12-31&endDate=2025-01-01')
    total = data.get('totalPieces',data.get('summary',{}).get('totalPieces',data.get('overview',{}).get('totalPieces')))
    # Duration displays hours rather than pieces; its day labels still must stay in range.
    if endpoint=='duration':
        r.check(row, '补测跨年时长统计返回明细，未混入区间外生产日期',
                data['total']>0 and all('2025-01-02' not in str(x) for x in data['list']), data)
    else:
        r.check(row,'补测跨年'+endpoint+'已知7+11件合计18，不含次日13件',total==18,data.get('summary',data.get('overview')))
r.check(46, '补测跨年反向日期与正向返回完全一致', trend('2025-01-02','2024-12-30')==cross)
for query in ('startDate=1900-01-01&endDate=2030-01-01','startDate=1900-01-01','endDate=2099-01-01'):
    status, result = r.call('GET','/statistics/production-trend?'+query)
    r.check(45,'补测一年跨度限制覆盖单边参数：'+query,status==400,result.get('message'))
today=dt.date.today()
for key,delta in [('startDate',-1),('endDate',1)]:
    status,result=r.call('GET',f'/statistics/production-trend?{key}={today+dt.timedelta(days=delta)}')
    points=result.get('data',{}).get('productionTrend',[])
    expected=[(today+dt.timedelta(days=i)).isoformat() for i in (range(-1,1) if delta<0 else range(0,2))]
    r.check(45,'补测合法单边日期使用今天补齐另一端：'+key,status==200 and [x['date'] for x in points]==expected,points)
for span in (366,367):
    status,result=r.call('GET',f'/statistics/production-trend?startDate={today-dt.timedelta(days=span)}')
    r.check(45,'补测单边日期现行366天跨度边界：'+str(span),
            status==200 and len(result['data']['productionTrend'])==367 if span==366 else status==400,result.get('message'))
for query in ('startDate=2025-02-29&endDate=2025-03-01','startDate=2024-2-29&endDate=2024-03-01'):
    status,result=r.call('GET','/statistics/production-trend?'+query)
    r.check(45,'补测无效闰日或非规范日期格式被拒绝：'+query,status==400,result.get('message'))
db.close()
sys.exit(int(bool(r.failures)))
