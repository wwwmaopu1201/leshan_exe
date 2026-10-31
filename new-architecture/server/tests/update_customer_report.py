"""Reconcile per-case evidence into the customer workbook row ledger.

Partial coverage is deliberately not promoted to whole-row acceptance.
"""
import collections
import csv
import json
from pathlib import Path
csv.field_size_limit(10_000_000)

directory = Path(__file__).resolve().parents[3] / 'docs/qt-customer-regression-2026-10-23'
path = directory / '客户问题逐条记录.csv'
with path.open(encoding='utf-8-sig', newline='') as source:
    reader = csv.DictReader(source)
    fields, rows = reader.fieldnames, list(reader)
latest = {}
for line in (directory / 'customer-cases.jsonl').read_text().splitlines():
    result = json.loads(line)
    latest[result['name']] = result
catalogs = {
    '/device/type-summary': dict(empty=[128], duplicate=[130], time=[139], length=[130], order=[133], search=[]),
    '/device/electric-control-type-summary': dict(empty=[142],duplicate=[142,143],time=[142],length=[142],order=[142],search=[]),
    '/pattern/type-summary': dict(empty=[233],duplicate=[235],time=[243],length=[235],order=[238],search=[232]),
    '/pattern/order-summary': dict(empty=[247],duplicate=[249],time=[256],length=[249],order=[251],search=[246]),
}
evidence = collections.defaultdict(list)
for name, result in latest.items():
    matching = result['rows']
    for route, mapping in catalogs.items():
        if not name.startswith(route):
            continue
        key = ('empty' if '空名称' in name else 'duplicate' if '重复新增' in name or '已存在名称' in name
               else 'time' if '有更新时间' in name else 'length' if '超100' in name
               else 'order' if '倒序' in name else 'search')
        matching = mapping[key]
    # Date-format tests support input validation, not every production-chart issue.
    if '非法日期拒绝' in name:
        matching = []
    for number in matching:
        evidence[number].append(name + ': ' + result['result'])

fixed = {
    80:'同组设备名称新增/编辑重复校验',107:'同组设备名称新增/编辑重复校验',451:'同组设备名称新增/编辑重复校验',
    130:'目录100字符限制和重命名重复校验/事务',142:'公共目录校验、时间及排序',143:'公共目录重命名事务',
    133:'目录按更新时间倒序',139:'从目录自身读取更新时间',
    235:'目录100字符限制和重命名重复校验/事务',238:'目录按更新时间倒序',243:'从目录自身读取更新时间',
    249:'目录100字符限制和重命名重复校验/事务',251:'目录按更新时间倒序',256:'从目录自身读取更新时间',
    319:'恢复软删除员工原ID后重新导入，保留历史关联',
}
api_complete = {80,107,451,133,139,238,243,251,256,319,408,420,421,423}
for row in rows:
    number, title = int(row['原始行号']), row['客户问题']
    row['本轮状态'] = '待测试（未判定）'
    row['测试方式与证据'] = '尚无覆盖该条完整操作步骤的本轮证据；不能依据模块自测通过判定此条通过。'
    row['无法测试原因'] = ''
    row['修复及复测'] = ''
    if not title:
        row['本轮状态'] = '待明确复现条件'
        row['无法测试原因'] = '原记录无文字标题，需要结合该行嵌入截图/视频确认具体问题；尚未逐张解析附件。'
    if number in (4,5,348):
        row['本轮状态'] = '环境限制，未验证'
        row['无法测试原因'] = '客户步骤针对Windows .exe安装卸载；当前为macOS，缺Windows运行环境。'
    if number in (390,391,392):
        row['本轮状态'] = '环境限制，未验证'
        row['无法测试原因'] = 'QODBC/FreeTDS已独立准备，SQL Server官方镜像已下载；用户明确不授权接受微软EULA，因此未启动SQL Server容器，记录为暂未实测。当前ARM主机运行amd64镜像还存在官方不支持的模拟架构限制，不能替代Windows现场验收。'
    if number==395:
        row['本轮状态'] = '待明确数据库类型'
        row['无法测试原因'] = '原表仅写其他数据库类型，未说明产品和版本；当前实现支持MySQL/MSSQL，需指定其他目标才能实施兼容性验证。'
    if title == '未发现问题':
        row['本轮状态'] = '原记录非缺陷，待确认验收范围'
        row['无法测试原因'] = '原条目未描述失败或具体步骤，不能将其直接计作本轮测试通过。'
    if number in evidence:
        row['本轮状态'] = '部分测试通过（接口层）'
        row['测试方式与证据'] = '; '.join(evidence[number]) + '；详见customer-cases.jsonl（同名最后一次为最新结果）。'
        if number in api_complete:
            row['本轮状态'] = '测试通过（接口层）'
        if number in fixed:
            row['修复及复测'] = fixed[number] + '；修复后相关专项PASS。界面交互仍按测试范围单独验收。'
    if number in (170,173,174,175):
        row['本轮状态'] = '模拟设备测试通过，实机待验证'
        row['测试方式与证据'] = 'batch_transfer_cases.py.log：10个20KB文件约5.17秒、354KB文件约0.59秒；逐字节一致、无重复缺失，任务均completed。'
        row['无法测试原因'] = '没有连接客户真实设备，不能证明设备固件、实际网络下同样通过。'
    if number in (343,344):
        row['本轮状态'] = '部分测试通过（客户端通用表单）'
        row['测试方式与证据'] = '客户端ui-self-test新增员工表单复制粘贴、鼠标Leave不关闭检查；只覆盖该表单，不能代表所有输入框/弹窗。'

row_journal = directory / 'row-results.jsonl'
if row_journal.exists():
    last = {}
    for line in row_journal.read_text().splitlines():
        item = json.loads(line)
        if item['name'] in ('扩大设备树读取后仍限制账号分组，不泄露隔离根设备','设备全模型同样应用账号分组权限，隔离根设备不返回'):
            continue  # Harness incorrectly assumed account groups restrict business data; legacy contract and api_smoke explicitly allow cross-group visibility.
        if item['name'] == '创建10级分组数据不丢失父子关系' or '常规子串语义' in item['name']:
            continue  # Superseded harness placeholder / rule explicitly changed by the user.
        if (item['row']==133 and '搜索准确返回07' in item['name']) or (item['row']==251 and '输入数字1' in item['name']):
            continue  # Early catalog harness mapped supplementary checks to unrelated row numbers.
        if item['name']=='00时两设备在线，01时仅一设备在线':
            continue  # Replaced by a fixture that stores sessions in UTC, as the real server does.
        if item['row']==324 and item['name']=='员工实际导出XLSX，前导零工号和手机号按文本保存':
            continue  # Export belongs to source row 325; 324 is pagination.
        if item['row']==138 and item['name'].startswith('/device/electric-control-types'):
            continue  # Electric control links belong to source row145; row138 is device types.
        if item['name']=='历史重复手机号匹配多员工时拒绝上工，不随意选择第一个':
            continue  # Experimental F2 phone binding was deferred by the user; uniqueness tested separately.
        if item['name'] in ('批量删除可停止后续任务，当前应答完成后只删A而保留B','实际选择B和D一次删除两项，列表仅移除已成功文件'):
            continue  # Old harness assumed A preceded B; the API orders rows by current IDs. Replaced with displayed-order checks.
        last[(item['row'],item['name'])] = item
    grouped = collections.defaultdict(list)
    for item in last.values():
        grouped[item['row']].append(item)
    for row in rows:
        number = int(row['原始行号'])
        if number not in grouped:
            continue
        checks = grouped[number]
        passed = all(item['result'] == 'PASS' for item in checks)
        complete = number in (322,388,389,393,394,8,9,10,11,12,13,14,19,24,27,29,30,31,32,40,46,65,66,67,96,98,104,105,106,108,116,
                              128,129,131,132,136,137,138,157,160,161,164,165,166,167,180,181,183,184,188,200,201,205,206,207,208,209,211,214,215,
                              232,233,234,236,237,240,241,242,246,247,248,250,254,255,351,
                              44,63,68,75,77,78,86,89,125,163,169,177,226,227,313,362,406,415,424,425,426,432,435,442,448,460,462,469,475,479,480,481,16,60,61,62,72,82,87,88,90,111,114,120,122,127,261,266,270,273,274,275,276,277,278,279,283,284,285,286,287,309,317,318,321,323,324,329,330,331,333,340,363,364,367,371,375,376,378,380,381,382,383,384,385,386,400,401,402,403,404,405,414,416,417,418,419,420,421,423,430,431,436,437,439,440,441,443,446,461,466,474,477,478)
        row['本轮状态'] = ('测试通过（已注明范围）' if complete else '部分测试通过（已注明范围）') if passed else '发现问题，待修复/复测'
        scopes = ' / '.join(dict.fromkeys(item['scope'] for item in checks))
        if len(checks)>60:
            totals=collections.Counter(item['result'] for item in checks)
            failed=[item['name'] for item in checks if item['result']!='PASS']
            description='本行'+str(len(checks))+'项即时检查最后结果：'+str(dict(totals))+'；完整明细见row-results.jsonl、窗口兼容性、交互异常恢复及非Windows可靠性报告，检查数量不等于客户问题条数。'
            if failed:description+='；未通过：'+'; '.join(failed[:20])
            row['测试方式与证据']=scopes+'；'+description
        else:
            row['测试方式与证据'] = scopes+'；'+ '; '.join(item['name']+': '+item['result'] for item in checks) + '；row-results.jsonl逐项即时记录。'
        if number in (7,9,11,12,13,351):
            row['修复及复测'] = '登录修复：严格IPv4/主机名校验、5秒超时、三处Enter、明暗文图标、按服务器和账号恢复密码；对应日志最新结果为准。'
        if number == 15:
            row['无法测试原因'] = '本机替身已验证服务器恢复自动清除旧网络错误和手动重试；未切断用户真实网络，物理断网恢复待现场复测。'
        if number in (18,19,21,24,50,52,53,54,55,58,59):
            row['修复及复测'] = '本轮修复：首页加工/开机时长区分、失败清旧数据、时分翻译、表头对齐；看板筛选会话范围、设备树状态刷新与图例；图表大数字与Hover定位。按本行具体日志确认结果。'
        if number in (80,91,92):
            row['修复及复测'] = '分组移动统一校验所有设备和目标组重名，事务提交避免假成功/部分修改；两端删除分组共用设备转至未分组的事务。'
        if number in (32,96):
            row['修复及复测'] = '用户本轮明确确认：只排当前开机设备；设备名双向包含匹配。已按此规则修改并补回归。'
        if number in (98,104,105,106,108,116):
            row['修复及复测'] = '设备编号文案统一；设备名称输入上限100字、必填提示和输入时校空；设备编辑取消/X经放弃确认后关闭。'
        if number in (138,242,255):
            row['修复及复测'] = '关联数量跳转时清除上一次目录条件并重置分组选择；重置按钮也清除关联筛选。'
        if number in (166,168,183,184,196,198,199,200,201,208,209,211,214,215,223):
            row['修复及复测'] = '花型修复：显式空名称拒绝、控件长度/数值限制、占位文字不入库、批量改动不清零其他字段、列表快照定位旧ID、默认重命名、内联校验、预览长文本滚动、上传队列在自身模态窗口内刷新、无效按钮置灰及清理确认。具体覆盖见本行日志。'
        if number in (311,312,317,318,319,320):
            row['修复及复测']='工号统一1至11位数字、保留前导零；导入显式覆盖开关、逐行错误；恢复软删除原ID保留历史；手机号去空格；原生服务端与客户端校验同步。以本行具体范围为准。'
        if number in (375,376,378,380,381,382,383,384,385,386):
            row['修复及复测']='服务端数据库界面加入配置脏状态、测试/同步权限门禁、类型切换清空旧字段及默认端口、密码图标和地址内联校验；真实MySQL只读/可写账号分别复测。'
        if number in (388,389,393,394):
            row['修复及复测']='独立MySQL8.4和真实QMYSQL驱动已覆盖手动、增量、切库及真实60秒定时调度：7表行数一致，关闭跨完整周期不执行，重开下一周期增量成功，中文/emoji与前导零保持；修复UTC水位和切库补齐。SQL Server仍按用户要求未实测，另列390-392。'
        if number in (416,418,420,421,423):
            row['修复及复测']='原生账号表单新增提交前红字校验；密码确认、邮箱格式/100字符限制、手机号去空格；验证失败保留当前表单。'
        if number in (16,17):
            row['修复及复测']='客户端顶栏同时明确标注客户端/服务端版本、管理API地址及设备TCP端口；登录接口返回实际设备TCP端口。当前为本机连接验证，客户远端IP连通性未验证。'
        if number==325:
            row['修复及复测']='员工导出增加默认XLSX（保留CSV选择），工号/手机号写为文本、保留前导零，增加创建时间列和自动列宽；实际点击客户端导出并用openpyxl复核生成文件。Excel本机显示仍未现场验证。'
        if number in (363,364,366,367):
            row['修复及复测']='共享目录的原生服务端与API共用校验，拒绝相对路径、非法字符和超过系统限制的路径；留空可保存。原表约75字符的Windows路径未超Windows通用路径限制，不任意设短上限，具体业务上限尚未确认。'
        if number==371:
            row['修复及复测']='补充本机IP结果标题的英文翻译；交替打开主机名/IP/主机名，实际窗口标题各自正确。'
        if number in (60,61,62):
            row['修复及复测']='看板图表Hover增加对应字段名称及数值单位；运行/加工时长转换为时分秒，并随语言切换；使用率含百分号。真实图表鼠标事件复测通过。'
        if number in (329,330,333):
            row['修复及复测']='个人资料错误移至各输入框下方，支持空格手机号并规范化；姓名限制50字、摘要换行和完整Tooltip；修改密码三处均加入明暗文及图标切换。'
        if number in (336,338,339):
            row['无法测试原因']='已拦截并核对Qt实际产生的地图HTTPS地址/邮件草稿URL及提示；未向外发送邮件，未验证用户系统默认浏览器、邮件程序或邮件投递。当前留言入口是邮件草稿，没有软件内留言收件箱。'
        if number==340:
            row['修复及复测']='复现Qt资源仍引用旧版31页星火说明书；改为独立5页博尔Qt操作说明，按当前源码编写，逐页渲染检查；实际下载文件与新资源逐字节一致。旧版目录原PDF未改动。'
        if number==473:
            row['修复及复测']='真实TCP+原生服务端删除复现旧会话未关闭，补上主动断开；删除后旧连接断开，同编号重新识别无需重启。客户固件与现场网络待实机复测。'
        if number==264:
            row['修复及复测']='实际DT传输复现取消/暂停HTTP已成功后迟到ACK仍发下一帧；在处理ACK前重新校验任务状态，停止后不再推进发送。已发送帧无法撤回，设备固件行为需现场验证。'
        if number in (263,264,265):
            row['无法测试原因']='已覆盖真实Qt服务端和TCP模拟设备；没有客户设备断电、固件重连及实际写入行为的现场证据。'
        if number in (277,279,276,286):
            row['修复及复测']='统计页明确搜索时不再自动改回历史日期；设备名接口和客户端筛选均执行双向包含；设备树加入状态图标和颜色图例。使用真实Qt两端和已知业务夹具复测。'
        if number in (299,304):
            row['测试方式与证据']+=' 当前实现：已处理率=已处理次数/报警总次数；日均报警时长=当日报警秒数合计/次数，再换算为分钟。这里只验证实现与已知数据一致。'
        if number in (72,82,87,88,440,441,443):
            row['修复及复测']='客户端补真实顶级选项并保留空父级；原生服务端未分组菜单提供新增顶级；所有入口共享名称/父级/同级重名校验；编辑禁改父级，生产线不再误跳过新顶级实体。两端实际右键及接口复测。'
        if number in (90,461):
            row['修复及复测']='两端设备树列按内容伸展，启用按像素横向滚动；创建十级实体组后实际滚动到末级并验证完整名称宽度。'
        if number in (436,437):
            row['修复及复测']='服务端默认不暗中选中实体总分组；实体分组按自身和后代筛选，其他顶级组/未分组不混入；清空恢复全部设备并同步标签。用25+1+1台已知设备逐项核对。'
        if number in (439,466):
            row['修复及复测']='原生服务端设备树加入状态图标及三色图例；设备列表缝纫改红、空闲绿、关机灰，与客户端一致。'
        if number in (110,111,120,122,127):
            row['修复及复测']='快速新增设备/电控类型统一100字符限制，必填星号与空值红字；在真实控件切换中英文验证初始名称、状态枚举与新增类型业务值不被改写。'
        if number==99:
            row['无法测试原因']='本地协议5.03提供UINT设备型号，已用DT帧验证modelName回读；资料未提供该数字型号与界面电控类型名称的映射，不能把两种字段擅自混写，真实电控名称默认填充仍待型号映射资料。'
        if number==313:
            row['修复及复测']='按用户确认禁止活跃员工手机号重复；共享写入规范空白并查重，新增/修改/导入/覆盖/恢复均覆盖，自身编辑排除本ID，恢复遇号码已被他人占用时拒绝。客户端和原生表单、SQLite/MySQL均复测通过。'
        if number in (163,169,253,257):
            row['修复及复测']='按用户确认订单号仅允许ASCII字母数字（保留前导零），禁止空格汉字特殊符号；上传/单改/批改/目录改名及原生写入统一校验，空元数据允许清除关联。具体结果以本行记录为准。'
        if number in (406,432):
            row['修复及复测']='服务端权限摘要和账号预览补齐细分权限，按客户端相同的旧父级权限兼容与显式子权限规则显示；实测不混入未授权的下发日志和状态统计。'
        if number==462:
            row['修复及复测']='服务端设备选择跨分组保留，增加移动已选设备到当前组菜单及取消选择入口；实际跨两个分组勾选后一次移动两台，操作成功清理已选项。'
        if number in (77,78,125,448):
            row['修复及复测']='设备树右键新增真正删除设备，与移出分组分开；未分组设备不再显示无意义移出操作；删除需要二次确认，取消保留，确认删除记录。具体两端证据以本行范围为准。'
        if number==362:
            row['修复及复测']='当前服务端口进程从管理HTTP端口改为设备TCP端口；端口输入默认同步设备TCP端口（未监听时38400），实际lsof结果包含本轮模拟设备监听端口。'
        if number in (289,291,307):
            row['修复及复测']='平均缝纫速度明确单位针/分钟；已知针数和加工时间验证公式。报警文案优先描述并兼容旧类型的编号前缀，界面隐藏单独编号列，API保留代码供诊断；固件未定义的505不猜测映射。具体以本行记录为准。'
        if number==177:
            row['修复及复测']='修复英文界面仍使用中文ZIP默认文件名；两种语言实际保存下载，检查文件名、Save/保存按钮、PK签名和完成提示。'
        if number==475:
            row['修复及复测']='API查询值显式FullyDecoded，恢复百分号编码的保留字符；真实失败登录日志按!、+、&、?、#、%、_、/和中文搜索，原生日志搜索同步实测。'
        if number==424:
            row['修复及复测']='按原表建议，账号新建/编辑手机号必填，与客户端个人资料一致；界面星号、空值红字、API拒绝清空及空格规范化已验证。不把员工手机号唯一性规则套用于登录账号。'
        if number in (100,113):
            row['无法测试原因']='用户要求设备工号/姓名先留着不动，由用户确认；保留既有编辑及绑定行为。V1.2协议手机号与当前工号实现的差异也暂缓适配。'
        if number==118:
            row['修复及复测']='设备行增加受权限约束的监控入口并选中对应设备；修复历史统计覆盖实时转速和累计针数，只由实时设备字段提供指标；历史接口仅更新趋势图。这里只覆盖数据监控，未将其等同屏幕远控。'
        if number==468:
            row['修复及复测']='按本地协议0x0B29/0x0003核对总生产量与当前生产量，单位件；界面明确显示当前/总生产量(件)，测试值3/8。'
        if number==119:
            row['修复及复测']='当前截图发现英文顶栏版本说明自动换多行后裁切；改为固定两行及足够高度，完整文字保留Tooltip；1120宽窗口几何与截图均复核。其他英文长字段仍按各专项范围验收。'
        if number in (278,280):
            row['修复及复测']='工资区间控件作为整体换行，长数字不挤压相邻控件；修复表格/总额筛选到10元而排行/日趋势仍为30元，所有工资图表改为同一筛选结果重新汇总，并实际Hover核对10元。'
        if number in (273,283):
            row['修复及复测']='补入未分组零产量记录复现统计页全部分组遗漏未分组；虚拟全部范围不再附带仅含已分组设备的ID过滤。按可见工资/加工页面核对3条全量、1条未分组及清空恢复。'
        if number in (303,309):
            row['修复及复测']='饼图Hover从横向位置索引改为实际扇区角度，图形按可用尺寸放大，多类别增加不同颜色，圆外清除提示；报警趋势Hover显示报警次数及平均时长，字段与分钟单位随语言切换。'
        if number in (405,407):
            row['修复及复测']='角色新建/编辑在字段或权限改变后关闭弹窗，先询问是否放弃；取消保留输入，确认关闭不返回保存数据。'
        if number==64:
            row['修复及复测']='看板统计卡片按可用宽度在两列/四列间布局，英文标题和说明允许换行；1120×700实际窗口复测无横向滚动，标题不裁切。'
        if number==85:
            row['修复及复测']='分组菜单支持实际删除；弹出前与显示后均按窗口/屏幕交集约束位置。复测使用全局坐标校验弹出窗口，1120×700菜单完整可见。'
        if number in (190,191):
            row['修复及复测']='设备树重建屏蔽中间勾选事件并保留设备/勾选，避免切页后误清空当前设备；文件请求以序号防旧响应覆盖。实际DT设备工作、切至监控再返回、断开后自动关机并清空文件，旧回传请求拒绝。'
        if number==470:
            row['修复及复测']='设备空编号/名称校验移到对话框接受之前，空名称保留表单显示错误；实测100字符限制、复制与鼠标Leave。员工字段维持用户要求的原逻辑。'
        if number in (245,267):
            row['修复及复测']='补齐确认操作、清除已完成及其确认/取消任务提示翻译；实际打开相应确认框逐项核对英文标签和按钮。'
        if number in (377,387):
            row['修复及复测']='数据库连接失败按认证/访问权限、库不存在、地址端口连接失败提供中文可操作提示并支持英文；用真实MySQL错误密码、不存在数据库、拒绝连接复测，原生GUI也验证错误密码与同步禁用。'
        if number==45:
            row['修复及复测']='补测发现单边日期参数绕过一年跨度校验；产量趋势改为按补齐今天后的实际区间校验。超长单边/双边拒绝、合法单边返回、现行366/367天边界、无效闰日/日期格式均复测通过。'
        if number==322:
            row['修复及复测']='接口之外，新增客户端实际批量删除有生产/薪资历史的员工；返回工资页面仍有原工号姓名、15件30元；实际导出全部/合并两种XLSX并独立读取，前导零、件数金额和数值类型一致；修复合计金额被写成文本导致Excel求和可能忽略的问题。删除为软删除，保留历史。'
        if number==325:
            row['修复及复测']+='；补测发现通用XLSX导出器缺少默认Normal样式，已补齐，实际生成文件经openpyxl读取不再出现缺省样式警告。'
        if number in (345,482):
            row['修复及复测']='窗口专项：21个客户端页面/9个服务端页面，中英文及1000至1920宽布局；离屏100/125/150/200% Qt缩放和原生macOS100/200%，共18组含独立服务端登录窗。修复花型工具栏/标题/地址撑宽、可用屏幕适配与小屏滚动、登录表单首次可见、弹窗缩放/标题/遮罩，以及长选项下拉框越屏、长提示固定高度裁切；离屏100/200%还验证Qt全屏退出恢复。业务和标准回归通过。'
        if number in (345,482):
            row['修复及复测']+='；交互专项新增8组离屏/原生100/200%配置，修复连续通知旧定时器、Enter默认按钮和模态关闭焦点恢复。另测大表格、最小化、嵌套弹窗、迟到响应销毁、三轮进程异常退出及上传目录不可创建后恢复；详见交互与异常恢复补测报告。'
            row['无法测试原因']='已验证本机Qt窗口/键盘/输入法事件注入与隔离故障恢复。Windows系统DPI/字体/多屏/RDP、真实输入法候选窗、休眠锁屏、真实断电/磁盘满/ACL和长期并发尚未覆盖，原因详见交互与异常恢复补测报告；不判全平台验收。'
        if number in (438,474):
            row['修复及复测']+='；补测修复服务端本地模型/设备树1000条及客户端本地模型2000条截断；真实SQLite1225条服务端分页与2250条API/客户端树/模型/早期设备选择一致，普通接口分页契约保留。'
        if number==172:
            row['修复及复测']+='；本轮补测真实定时器发现UTC队列时间按本地解释导致新任务立即过期，现按UTC计算，SQLite及真实MySQL新建等待/暂停恢复/11分钟超时均通过，三设备独立传输故障隔离复测通过。'
        if number==313:
            row['修复及复测']+='；16并发相同手机号员工新增只成功1条，其余明确拒绝，未产生重复。'
        if number in (438,474):
            row['修复及复测']+='；已补真实MySQL2250条分页/树/全模型/客户端选择；列表分组映射一次读取，消除逐设备重复查询。'
        if number in (345,482):
            row['修复及复测']+='；非Windows可靠性专项：客户端异常JSON/业务码误判及服务地址切换旧响应隔离；16并发HTTP、4个真实Qt客户端、300秒30093次请求；真实POSIX权限、32MB独占虚拟卷上传/下载/数据库ENOSPC失败和恢复、异常退出旧会话复用均通过。数据库存储错误提供可读中英文提示。'
            row['无法测试原因']='Windows系统DPI/字体/多屏/RDP仍缺环境；真实系统拼音候选窗、休眠锁屏/唤醒、辅助技术、真实物理断电/介质损坏与数小时数天长期并发未验证。已实测POSIX目录不可写、真实受限虚拟卷ENOSPC及5分钟并发，不能扩大成未覆盖范围。详见非Windows可靠性补测报告。'
        if any(item['result']=='BLOCKED' for item in checks):
            row['本轮状态']=next(item.get('blockedStatus','已确认功能缺失，待外部资料') for item in checks if item['result']=='BLOCKED')
            row['无法测试原因']='；'.join(item.get('reason','') for item in checks if item['result']=='BLOCKED')

for row in rows:
    if int(row['原始行号']) in (100,113):
        row['本轮状态']='用户暂缓，待确认员工规则'
        row['无法测试原因']='用户明确要求留着不动，由用户与客户确认；设备员工编辑/绑定保持既有逻辑，手机号协议适配实验已撤回，暂不判通过。'
    if not row['客户问题']:
        row['本轮状态'] = '原记录空条目，无法执行'
        row['无法测试原因'] = '已复查原表：仅有编号，无文字问题/步骤/预期；也没有锚定该行的图片附件，无法确定测试对象。'
    if int(row['原始行号'])==192:
        row['无法测试原因']='已验证回传覆盖保留元数据，回传后可用同一编辑表单维护类型/针数/工价/订单；回传前独立维护流程尚未实现，原条目为业务流程疑问，不将回传后编辑等同回传前维护。'
    if int(row['原始行号'])==45:
        row['无法测试原因']='已实测一年跨度及默认日期、跨年闰日边界。当前允许选择未来日期并返回零，原表“未来时间可以禁用”为建议，禁用功能未作为已实现项；Windows日期控件现场显示也未验证。'
    if row['本轮状态'].startswith('部分测试通过') and not row['无法测试原因']:
        number=int(row['原始行号'])
        if number in (345,482):
            row['无法测试原因']='原条目为宽泛全局UI要求；已补测18组Qt缩放/原生Mac/登录与页面窗口配置，具体范围见窗口兼容性报告。仍无Windows实际系统DPI、客户字体和多显示器DPI迁移环境，不能判全局UI全部平台验收通过。'
        elif number in range(18,60) or number in (76,117,118) or number in range(186,225) or number in (263,265,289):
            row['无法测试原因']='本轮证据覆盖临时业务数据、实际Qt两端及本机DT协议模拟设备；缺客户电控固件、设备真实缝纫/断电及现场网络的数据，无法替代原场景实机验收。具体已执行步骤见证据列。'
        else:
            row['无法测试原因']='已执行证据列列明的本机Qt操作与断言；尚未在客户原Windows环境、原始数据和截图条件下重放该条全部路径，因此保留部分通过，未把局部检查扩大为整条验收。'

with path.open('w', encoding='utf-8-sig', newline='') as target:
    writer = csv.DictWriter(target, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)
counts = collections.Counter(row['本轮状态'] for row in rows)
(directory / 'ledger-counts.json').write_text(json.dumps(counts, ensure_ascii=False, indent=2))
progress=['# 当前测试进度（自动汇总）','',
          '这是持续更新的逐条台账，未测试和部分覆盖不计为整条验收通过。', '',
          '| 状态 | 原表行数 |','| --- | ---: |']
progress += [f'| {status} | {count} |' for status,count in counts.items()]
progress += ['', '## 当前确认的业务规则', '',
             '- 用户已确认：效率排名只包含当前开机设备。',
             '- 用户已确认：设备名称搜索采用双向包含匹配。',
             '- 用户已确认：员工手机号禁止重复；订单编号只允许ASCII字母和数字。',
             '- 用户暂缓：设备员工编辑/绑定与急停计件保持原逻辑，等待用户确认。',
             '- 用户不授权SQL Server容器启动，因此暂未实测；花型预览暂缓，继续其他。', '',
             '## 仍有失败或外部阻塞的条目', '']
for row in rows:
    if row['本轮状态'] in ('发现问题，待修复/复测','已确认功能缺失，待外部资料','协议定义不足，待确认计件规则','用户暂缓，待确认员工规则','缺少固件报警映射，实机待验证','平台限制，Windows中文输出未验证'):
        progress.append(f'- 第{row["原始行号"]}行：{row["客户问题"]}。{row["无法测试原因"] or "正在修复或补充复测证据"}')
progress += ['', '## 最近测试套件结果', '']
suite_file=directory/'suites.jsonl'
if suite_file.exists():
    suite_results={}
    for line in suite_file.read_text().splitlines():
        suite=json.loads(line);suite_results[suite['case']]=suite['exit']
    suite_results.pop('employee_protocol_cases.py',None)  # User deferred this experiment; historical PASS does not describe current code.
    for name,code in suite_results.items():progress.append(f'- {name}：'+('通过' if code==0 else f'上一次退出码{code}，需看日志或正在复测'))
progress += ['', '逐条证据：`客户问题逐条记录.csv`；即时记录：`row-results.jsonl`；每个脚本独立使用临时数据库。']
(directory/'当前测试进度.md').write_text('\n'.join(progress)+'\n',encoding='utf-8')
print(json.dumps(counts, ensure_ascii=False, indent=2))
