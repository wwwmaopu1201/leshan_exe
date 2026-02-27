-- 博尔局域网管理软件 - 完整测试数据脚本
-- 包含所有表的测试数据，可直接执行
-- Database: MySQL 5.7+

USE boer_lan;

-- ========================================
-- 清空现有数据（谨慎使用）
-- ========================================
SET FOREIGN_KEY_CHECKS = 0;
TRUNCATE TABLE login_logs;
TRUNCATE TABLE salary_records;
TRUNCATE TABLE alarm_records;
TRUNCATE TABLE production_records;
TRUNCATE TABLE employee_devices;
TRUNCATE TABLE download_tasks;
TRUNCATE TABLE patterns;
TRUNCATE TABLE employees;
TRUNCATE TABLE devices;
TRUNCATE TABLE device_groups;
TRUNCATE TABLE users;
SET FOREIGN_KEY_CHECKS = 1;

-- ========================================
-- 用户数据
-- ========================================
-- 密码都是 admin123 (bcrypt加密后的结果)
INSERT INTO users (id, username, password, nickname, email, phone, role, avatar, created_at, updated_at) VALUES
(1, 'admin', '$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK', '系统管理员', 'admin@boer.com', '13800000000', 'admin', NULL, NOW(), NOW()),
(2, 'zhangsan', '$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK', '张三', 'zhangsan@boer.com', '13800138001', 'user', NULL, NOW(), NOW()),
(3, 'lisi', '$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK', '李四', 'lisi@boer.com', '13800138002', 'user', NULL, NOW(), NOW()),
(4, 'wangwu', '$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK', '王五', 'wangwu@boer.com', '13800138003', 'user', NULL, NOW(), NOW());

-- ========================================
-- 登录日志数据
-- ========================================
INSERT INTO login_logs (user_id, ip, device, status, login_time) VALUES
(1, '127.0.0.1', 'Chrome/MacOS', 'success', DATE_SUB(NOW(), INTERVAL 1 HOUR)),
(1, '127.0.0.1', 'Chrome/MacOS', 'success', DATE_SUB(NOW(), INTERVAL 2 HOUR)),
(2, '192.168.1.100', 'Chrome/Windows', 'success', DATE_SUB(NOW(), INTERVAL 3 HOUR)),
(2, '192.168.1.100', 'Chrome/Windows', 'failed', DATE_SUB(NOW(), INTERVAL 5 HOUR)),
(3, '192.168.1.101', 'Chrome/Windows', 'success', DATE_SUB(NOW(), INTERVAL 8 HOUR)),
(1, '127.0.0.1', 'Chrome/MacOS', 'success', DATE_SUB(NOW(), INTERVAL 1 DAY)),
(2, '192.168.1.100', 'Chrome/Windows', 'success', DATE_SUB(NOW(), INTERVAL 1 DAY)),
(1, '127.0.0.1', 'Chrome/MacOS', 'success', DATE_SUB(NOW(), INTERVAL 2 DAY));

-- ========================================
-- 设备分组数据
-- ========================================
INSERT INTO device_groups (id, name, parent_id, created_at) VALUES
(1, '全部设备', NULL, NOW()),
(2, 'A车间', 1, NOW()),
(3, 'B车间', 1, NOW()),
(4, 'C车间', 1, NOW()),
(5, 'A车间-1号线', 2, NOW()),
(6, 'A车间-2号线', 2, NOW()),
(7, 'B车间-1号线', 3, NOW()),
(8, 'B车间-2号线', 3, NOW());

-- ========================================
-- 设备数据 (20台设备)
-- ========================================
INSERT INTO devices (id, code, name, type, model_name, ip, status, group_id, last_online, created_at) VALUES
-- A车间-1号线 (5台)
(1, 'A-001', '缝纫机A-001', '缝纫机', 'BM-2000', '192.168.1.101', 'online', 5, NOW(), NOW()),
(2, 'A-002', '缝纫机A-002', '缝纫机', 'BM-2000', '192.168.1.102', 'working', 5, NOW(), NOW()),
(3, 'A-003', '缝纫机A-003', '缝纫机', 'BM-3000', '192.168.1.103', 'working', 5, NOW(), NOW()),
(4, 'A-004', '缝纫机A-004', '缝纫机', 'BM-2000', '192.168.1.104', 'idle', 5, NOW(), NOW()),
(5, 'A-005', '缝纫机A-005', '缝纫机', 'BM-3000', '192.168.1.105', 'online', 5, NOW(), NOW()),
-- A车间-2号线 (5台)
(6, 'A-006', '缝纫机A-006', '缝纫机', 'BM-3000', '192.168.1.106', 'working', 6, NOW(), NOW()),
(7, 'A-007', '缝纫机A-007', '缝纫机', 'BM-2000', '192.168.1.107', 'offline', 6, DATE_SUB(NOW(), INTERVAL 2 HOUR), NOW()),
(8, 'A-008', '缝纫机A-008', '缝纫机', 'BM-5000', '192.168.1.108', 'working', 6, NOW(), NOW()),
(9, 'A-009', '缝纫机A-009', '缝纫机', 'BM-2000', '192.168.1.109', 'alarm', 6, NOW(), NOW()),
(10, 'A-010', '缝纫机A-010', '缝纫机', 'BM-3000', '192.168.1.110', 'online', 6, NOW(), NOW()),
-- B车间-1号线 (5台)
(11, 'B-001', '缝纫机B-001', '缝纫机', 'BM-3000', '192.168.1.111', 'working', 7, NOW(), NOW()),
(12, 'B-002', '缝纫机B-002', '缝纫机', 'BM-5000', '192.168.1.112', 'working', 7, NOW(), NOW()),
(13, 'B-003', '缝纫机B-003', '缝纫机', 'BM-2000', '192.168.1.113', 'idle', 7, NOW(), NOW()),
(14, 'B-004', '缝纫机B-004', '缝纫机', 'BM-3000', '192.168.1.114', 'online', 7, NOW(), NOW()),
(15, 'B-005', '缝纫机B-005', '缝纫机', 'BM-2000', '192.168.1.115', 'offline', 7, DATE_SUB(NOW(), INTERVAL 1 DAY), NOW()),
-- B车间-2号线 (5台)
(16, 'B-006', '缝纫机B-006', '缝纫机', 'BM-5000', '192.168.1.116', 'working', 8, NOW(), NOW()),
(17, 'B-007', '缝纫机B-007', '缝纫机', 'BM-3000', '192.168.1.117', 'working', 8, NOW(), NOW()),
(18, 'B-008', '缝纫机B-008', '缝纫机', 'BM-2000', '192.168.1.118', 'alarm', 8, NOW(), NOW()),
(19, 'B-009', '缝纫机B-009', '缝纫机', 'BM-3000', '192.168.1.119', 'online', 8, NOW(), NOW()),
(20, 'B-010', '缝纫机B-010', '缝纫机', 'BM-5000', '192.168.1.120', 'working', 8, NOW(), NOW());

-- ========================================
-- 员工数据 (15名员工)
-- ========================================
INSERT INTO employees (id, code, name, department, position, phone, created_at) VALUES
(1, 'E001', '张三', '生产部', '操作员', '13800138001', NOW()),
(2, 'E002', '李四', '生产部', '操作员', '13800138002', NOW()),
(3, 'E003', '王五', '生产部', '组长', '13800138003', NOW()),
(4, 'E004', '赵六', '生产部', '操作员', '13800138004', NOW()),
(5, 'E005', '钱七', '生产部', '操作员', '13800138005', NOW()),
(6, 'E006', '孙八', '生产部', '操作员', '13800138006', NOW()),
(7, 'E007', '周九', '生产部', '组长', '13800138007', NOW()),
(8, 'E008', '吴十', '生产部', '操作员', '13800138008', NOW()),
(9, 'E009', '郑一', '质检部', '质检员', '13800138009', NOW()),
(10, 'E010', '刘二', '质检部', '质检员', '13800138010', NOW()),
(11, 'E011', '陈三', '技术部', '工程师', '13800138011', NOW()),
(12, 'E012', '林四', '技术部', '工程师', '13800138012', NOW()),
(13, 'E013', '黄五', '行政部', '主管', '13800138013', NOW()),
(14, 'E014', '杨六', '行政部', '文员', '13800138014', NOW()),
(15, 'E015', '许七', '生产部', '操作员', '13800138015', NOW());

-- ========================================
-- 员工设备绑定
-- ========================================
INSERT INTO employee_devices (employee_id, device_id, created_at) VALUES
(1, 1, NOW()), (1, 2, NOW()),
(2, 3, NOW()), (2, 4, NOW()),
(3, 5, NOW()), (3, 6, NOW()),
(4, 7, NOW()), (4, 8, NOW()),
(5, 9, NOW()), (5, 10, NOW()),
(6, 11, NOW()), (6, 12, NOW()),
(7, 13, NOW()), (7, 14, NOW()),
(8, 15, NOW()), (8, 16, NOW()),
(15, 17, NOW()), (15, 18, NOW());

-- ========================================
-- 花型文件数据
-- ========================================
INSERT INTO patterns (id, name, file_name, file_path, file_size, stitches, colors, width, height, uploaded_by, created_at) VALUES
(1, '经典碎花图案', 'pattern_001.dst', '/uploads/patterns/pattern_001.dst', 256000, 12580, 5, 120.5, 85.3, 1, DATE_SUB(NOW(), INTERVAL 7 DAY)),
(2, '几何线条设计', 'pattern_002.dst', '/uploads/patterns/pattern_002.dst', 185000, 8920, 3, 100.0, 100.0, 1, DATE_SUB(NOW(), INTERVAL 6 DAY)),
(3, '玫瑰花纹样', 'pattern_003.dst', '/uploads/patterns/pattern_003.dst', 320000, 15680, 8, 150.0, 120.0, 1, DATE_SUB(NOW(), INTERVAL 5 DAY)),
(4, '简约条纹', 'pattern_004.dst', '/uploads/patterns/pattern_004.dst', 89000, 4520, 2, 80.0, 60.0, 1, DATE_SUB(NOW(), INTERVAL 4 DAY)),
(5, '中国风祥云', 'pattern_005.dst', '/uploads/patterns/pattern_005.dst', 420000, 22350, 6, 200.0, 180.0, 1, DATE_SUB(NOW(), INTERVAL 3 DAY)),
(6, '卡通动物', 'pattern_006.dst', '/uploads/patterns/pattern_006.dst', 156000, 7890, 12, 110.0, 95.0, 1, DATE_SUB(NOW(), INTERVAL 2 DAY)),
(7, '民族风纹样', 'pattern_007.dst', '/uploads/patterns/pattern_007.dst', 289000, 13450, 7, 140.0, 110.0, 1, DATE_SUB(NOW(), INTERVAL 1 DAY)),
(8, '字母Logo', 'pattern_008.dst', '/uploads/patterns/pattern_008.dst', 52000, 2850, 1, 50.0, 30.0, 1, NOW());

-- ========================================
-- 下发任务数据
-- ========================================
INSERT INTO download_tasks (id, pattern_id, device_id, status, progress, message, operator_id, created_at, updated_at) VALUES
-- 等待中
(1, 1, 1, 'waiting', 0, NULL, 1, NOW(), NOW()),
(2, 1, 2, 'waiting', 0, NULL, 1, NOW(), NOW()),
(3, 2, 3, 'waiting', 0, NULL, 1, NOW(), NOW()),
-- 下载中
(4, 3, 5, 'downloading', 45, NULL, 1, DATE_SUB(NOW(), INTERVAL 5 MINUTE), NOW()),
(5, 3, 6, 'downloading', 78, NULL, 1, DATE_SUB(NOW(), INTERVAL 5 MINUTE), NOW()),
-- 已完成
(6, 1, 11, 'completed', 100, '下发成功', 1, DATE_SUB(NOW(), INTERVAL 1 HOUR), DATE_SUB(NOW(), INTERVAL 50 MINUTE)),
(7, 2, 12, 'completed', 100, '下发成功', 1, DATE_SUB(NOW(), INTERVAL 2 HOUR), DATE_SUB(NOW(), INTERVAL 1 HOUR)),
(8, 4, 13, 'completed', 100, '下发成功', 1, DATE_SUB(NOW(), INTERVAL 3 HOUR), DATE_SUB(NOW(), INTERVAL 2 HOUR)),
-- 失败
(9, 5, 7, 'failed', 0, '设备离线', 1, DATE_SUB(NOW(), INTERVAL 1 HOUR), DATE_SUB(NOW(), INTERVAL 1 HOUR)),
(10, 6, 15, 'failed', 0, '连接超时', 1, DATE_SUB(NOW(), INTERVAL 30 MINUTE), DATE_SUB(NOW(), INTERVAL 30 MINUTE));

-- ========================================
-- 生产记录数据 (近7天)
-- ========================================
INSERT INTO production_records (device_id, employee_id, pattern_id, pieces, stitches, thread_length, running_time, idle_time, record_date, created_at) VALUES
-- 今天
(1, 1, 1, 120, 1507600, 850.5, 7.5, 0.5, CURDATE(), NOW()),
(2, 1, 1, 135, 1698300, 920.8, 8.0, 0.0, CURDATE(), NOW()),
(3, 2, 2, 180, 1605600, 780.2, 7.8, 0.2, CURDATE(), NOW()),
(5, 3, 3, 95, 1489600, 1050.0, 7.0, 1.0, CURDATE(), NOW()),
(6, 3, 1, 142, 1786760, 960.5, 8.0, 0.0, CURDATE(), NOW()),
(8, 4, 2, 165, 1471800, 720.3, 7.2, 0.8, CURDATE(), NOW()),
(11, 6, 1, 128, 1610240, 880.6, 7.6, 0.4, CURDATE(), NOW()),
(12, 6, 3, 88, 1379840, 980.2, 6.8, 1.2, CURDATE(), NOW()),
(16, 8, 1, 155, 1949900, 1020.0, 8.0, 0.0, CURDATE(), NOW()),
(17, 15, 2, 148, 1320160, 650.8, 7.4, 0.6, CURDATE(), NOW()),
(20, 15, 3, 92, 1442240, 1010.5, 6.9, 1.1, CURDATE(), NOW()),
-- 昨天
(1, 1, 1, 125, 1573750, 880.0, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(2, 1, 2, 140, 1248800, 610.5, 7.5, 0.5, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(3, 2, 1, 168, 2114160, 1150.2, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(5, 3, 3, 85, 1333800, 950.0, 6.5, 1.5, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(6, 3, 2, 150, 1338000, 660.3, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(11, 6, 1, 132, 1660560, 900.8, 7.9, 0.1, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(12, 6, 2, 145, 1293400, 630.5, 7.6, 0.4, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(16, 8, 3, 78, 1223040, 870.2, 6.2, 1.8, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(17, 15, 1, 138, 1736040, 940.0, 7.7, 0.3, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
-- 前天
(1, 1, 2, 115, 1025800, 500.5, 7.0, 1.0, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(2, 1, 1, 128, 1610240, 880.0, 7.6, 0.4, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(3, 2, 3, 72, 1128960, 800.5, 5.8, 2.2, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(5, 3, 1, 145, 1824100, 990.0, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(6, 3, 2, 158, 1409360, 690.8, 7.9, 0.1, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(11, 6, 3, 68, 1066240, 760.2, 5.5, 2.5, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
(12, 6, 1, 142, 1786760, 970.5, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 2 DAY), DATE_SUB(NOW(), INTERVAL 2 DAY)),
-- 3天前
(1, 1, 1, 130, 1635400, 890.0, 7.9, 0.1, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
(2, 1, 3, 65, 1019200, 720.5, 5.2, 2.8, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
(3, 2, 1, 155, 1950100, 1060.2, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
(5, 3, 2, 162, 1444640, 710.0, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
(11, 6, 1, 138, 1736040, 950.8, 7.7, 0.3, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
(16, 8, 2, 170, 1515600, 745.5, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 3 DAY), DATE_SUB(NOW(), INTERVAL 3 DAY)),
-- 4天前
(1, 1, 2, 108, 962880, 470.0, 6.8, 1.2, DATE_SUB(CURDATE(), INTERVAL 4 DAY), DATE_SUB(NOW(), INTERVAL 4 DAY)),
(2, 1, 1, 122, 1534760, 835.0, 7.5, 0.5, DATE_SUB(CURDATE(), INTERVAL 4 DAY), DATE_SUB(NOW(), INTERVAL 4 DAY)),
(3, 2, 2, 175, 1560500, 765.8, 7.9, 0.1, DATE_SUB(CURDATE(), INTERVAL 4 DAY), DATE_SUB(NOW(), INTERVAL 4 DAY)),
(5, 3, 1, 140, 1761200, 960.2, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 4 DAY), DATE_SUB(NOW(), INTERVAL 4 DAY)),
(11, 6, 3, 82, 1285760, 915.0, 6.6, 1.4, DATE_SUB(CURDATE(), INTERVAL 4 DAY), DATE_SUB(NOW(), INTERVAL 4 DAY)),
-- 5天前
(1, 1, 1, 135, 1698300, 920.5, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 5 DAY), DATE_SUB(NOW(), INTERVAL 5 DAY)),
(2, 1, 2, 148, 1319360, 650.0, 7.6, 0.4, DATE_SUB(CURDATE(), INTERVAL 5 DAY), DATE_SUB(NOW(), INTERVAL 5 DAY)),
(3, 2, 1, 162, 2038760, 1110.8, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 5 DAY), DATE_SUB(NOW(), INTERVAL 5 DAY)),
(5, 3, 3, 75, 1176000, 835.5, 6.0, 2.0, DATE_SUB(CURDATE(), INTERVAL 5 DAY), DATE_SUB(NOW(), INTERVAL 5 DAY)),
(11, 6, 2, 155, 1382100, 680.2, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 5 DAY), DATE_SUB(NOW(), INTERVAL 5 DAY)),
-- 6天前
(1, 1, 3, 58, 909440, 645.0, 4.8, 3.2, DATE_SUB(CURDATE(), INTERVAL 6 DAY), DATE_SUB(NOW(), INTERVAL 6 DAY)),
(2, 1, 1, 118, 1484440, 810.5, 7.2, 0.8, DATE_SUB(CURDATE(), INTERVAL 6 DAY), DATE_SUB(NOW(), INTERVAL 6 DAY)),
(3, 2, 2, 165, 1471800, 720.0, 7.8, 0.2, DATE_SUB(CURDATE(), INTERVAL 6 DAY), DATE_SUB(NOW(), INTERVAL 6 DAY)),
(5, 3, 1, 152, 1911680, 1040.8, 8.0, 0.0, DATE_SUB(CURDATE(), INTERVAL 6 DAY), DATE_SUB(NOW(), INTERVAL 6 DAY)),
(11, 6, 1, 145, 1824100, 990.5, 7.9, 0.1, DATE_SUB(CURDATE(), INTERVAL 6 DAY), DATE_SUB(NOW(), INTERVAL 6 DAY));

-- ========================================
-- 报警记录数据
-- ========================================
INSERT INTO alarm_records (device_id, alarm_type, alarm_code, description, duration, status, start_time, end_time, created_at) VALUES
-- 今天
(9, '断线报警', 'E001', '主线断裂，请检查线轴', 300, 'resolved', DATE_SUB(NOW(), INTERVAL 2 HOUR), DATE_SUB(NOW(), INTERVAL 1 HOUR), DATE_SUB(NOW(), INTERVAL 2 HOUR)),
(18, '张力报警', 'E002', '线张力过大，请调整张力器', 180, 'pending', DATE_SUB(NOW(), INTERVAL 30 MINUTE), NULL, DATE_SUB(NOW(), INTERVAL 30 MINUTE)),
-- 昨天
(7, '断线报警', 'E001', '底线用尽', 120, 'resolved', DATE_SUB(NOW(), INTERVAL 26 HOUR), DATE_SUB(NOW(), INTERVAL 25 HOUR), DATE_SUB(NOW(), INTERVAL 26 HOUR)),
(3, '电机报警', 'E003', '主轴电机过热', 600, 'resolved', DATE_SUB(NOW(), INTERVAL 28 HOUR), DATE_SUB(NOW(), INTERVAL 27 HOUR), DATE_SUB(NOW(), INTERVAL 28 HOUR)),
(12, '传感器报警', 'E004', '断线传感器异常', 90, 'resolved', DATE_SUB(NOW(), INTERVAL 30 HOUR), DATE_SUB(NOW(), INTERVAL 29 HOUR), DATE_SUB(NOW(), INTERVAL 30 HOUR)),
-- 前天
(5, '断线报警', 'E001', '主线断裂', 150, 'resolved', DATE_SUB(NOW(), INTERVAL 50 HOUR), DATE_SUB(NOW(), INTERVAL 49 HOUR), DATE_SUB(NOW(), INTERVAL 50 HOUR)),
(8, '张力报警', 'E002', '张力不稳定', 240, 'resolved', DATE_SUB(NOW(), INTERVAL 52 HOUR), DATE_SUB(NOW(), INTERVAL 51 HOUR), DATE_SUB(NOW(), INTERVAL 52 HOUR)),
(16, '断线报警', 'E001', '底线断裂', 180, 'resolved', DATE_SUB(NOW(), INTERVAL 54 HOUR), DATE_SUB(NOW(), INTERVAL 53 HOUR), DATE_SUB(NOW(), INTERVAL 54 HOUR)),
-- 更早
(1, '电机报警', 'E003', '电机温度过高', 420, 'resolved', DATE_SUB(NOW(), INTERVAL 72 HOUR), DATE_SUB(NOW(), INTERVAL 71 HOUR), DATE_SUB(NOW(), INTERVAL 72 HOUR)),
(11, '传感器报警', 'E004', '位置传感器故障', 300, 'resolved', DATE_SUB(NOW(), INTERVAL 96 HOUR), DATE_SUB(NOW(), INTERVAL 95 HOUR), DATE_SUB(NOW(), INTERVAL 96 HOUR)),
(6, '断线报警', 'E001', '主线断裂', 200, 'resolved', DATE_SUB(NOW(), INTERVAL 100 HOUR), DATE_SUB(NOW(), INTERVAL 99 HOUR), DATE_SUB(NOW(), INTERVAL 100 HOUR)),
(17, '张力报警', 'E002', '张力过小', 150, 'resolved', DATE_SUB(NOW(), INTERVAL 120 HOUR), DATE_SUB(NOW(), INTERVAL 119 HOUR), DATE_SUB(NOW(), INTERVAL 120 HOUR)),
(2, '断线报警', 'E001', '底线用尽', 90, 'resolved', DATE_SUB(NOW(), INTERVAL 144 HOUR), DATE_SUB(NOW(), INTERVAL 143 HOUR), DATE_SUB(NOW(), INTERVAL 144 HOUR)),
(20, '电机报警', 'E003', '电机异响', 360, 'resolved', DATE_SUB(NOW(), INTERVAL 150 HOUR), DATE_SUB(NOW(), INTERVAL 149 HOUR), DATE_SUB(NOW(), INTERVAL 150 HOUR));

-- ========================================
-- 工资记录数据 (近30天汇总)
-- ========================================
INSERT INTO salary_records (employee_id, device_id, pieces, unit_price, salary, bonus, total_amount, record_date, created_at) VALUES
-- 本月
(1, 1, 1250, 0.50, 625.00, 80.00, 705.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(1, 2, 1180, 0.50, 590.00, 60.00, 650.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(2, 3, 1420, 0.50, 710.00, 100.00, 810.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(3, 5, 980, 0.55, 539.00, 50.00, 589.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(3, 6, 1350, 0.55, 742.50, 90.00, 832.50, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(4, 8, 1280, 0.50, 640.00, 70.00, 710.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(6, 11, 1150, 0.50, 575.00, 55.00, 630.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(6, 12, 890, 0.55, 489.50, 40.00, 529.50, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(8, 16, 1480, 0.50, 740.00, 110.00, 850.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(15, 17, 1320, 0.50, 660.00, 85.00, 745.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
(15, 20, 920, 0.55, 506.00, 45.00, 551.00, DATE_SUB(CURDATE(), INTERVAL 1 DAY), DATE_SUB(NOW(), INTERVAL 1 DAY)),
-- 上周
(1, 1, 5800, 0.50, 2900.00, 350.00, 3250.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(1, 2, 5500, 0.50, 2750.00, 300.00, 3050.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(2, 3, 6200, 0.50, 3100.00, 400.00, 3500.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(3, 5, 4500, 0.55, 2475.00, 250.00, 2725.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(3, 6, 5800, 0.55, 3190.00, 380.00, 3570.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(6, 11, 5200, 0.50, 2600.00, 280.00, 2880.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(8, 16, 6500, 0.50, 3250.00, 450.00, 3700.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY)),
(15, 17, 5900, 0.50, 2950.00, 360.00, 3310.00, DATE_SUB(CURDATE(), INTERVAL 8 DAY), DATE_SUB(NOW(), INTERVAL 8 DAY));

-- ========================================
-- 验证数据插入
-- ========================================
SELECT '数据插入完成，统计如下：' AS message;
SELECT 'users' AS table_name, COUNT(*) AS count FROM users
UNION ALL
SELECT 'login_logs', COUNT(*) FROM login_logs
UNION ALL
SELECT 'device_groups', COUNT(*) FROM device_groups
UNION ALL
SELECT 'devices', COUNT(*) FROM devices
UNION ALL
SELECT 'employees', COUNT(*) FROM employees
UNION ALL
SELECT 'employee_devices', COUNT(*) FROM employee_devices
UNION ALL
SELECT 'patterns', COUNT(*) FROM patterns
UNION ALL
SELECT 'download_tasks', COUNT(*) FROM download_tasks
UNION ALL
SELECT 'production_records', COUNT(*) FROM production_records
UNION ALL
SELECT 'alarm_records', COUNT(*) FROM alarm_records
UNION ALL
SELECT 'salary_records', COUNT(*) FROM salary_records;

-- ========================================
-- 测试数据说明
-- ========================================
-- 用户账号（密码都是 admin123）：
--   admin / admin123 - 系统管理员
--   zhangsan / admin123 - 普通用户
--   lisi / admin123 - 普通用户
--   wangwu / admin123 - 普通用户
--
-- 设备数量：20台（A车间10台，B车间10台）
-- 员工数量：15名
-- 花型文件：8个
-- 下发任务：10条（包含等待、下载中、已完成、失败）
-- 生产记录：近7天数据
-- 报警记录：14条
-- 工资记录：19条
-- 登录日志：8条
