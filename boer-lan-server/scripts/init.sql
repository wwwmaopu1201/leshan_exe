-- 博尔局域网管理软件 数据库初始化脚本
-- Database: MySQL 5.7+

-- 创建数据库
CREATE DATABASE IF NOT EXISTS boer_lan DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE boer_lan;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(255) NOT NULL,
    nickname VARCHAR(50),
    email VARCHAR(100),
    phone VARCHAR(20),
    role VARCHAR(20) DEFAULT 'user',
    avatar VARCHAR(255),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_username (username),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备分组表
CREATE TABLE IF NOT EXISTS device_groups (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    parent_id BIGINT UNSIGNED,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_parent_id (parent_id),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备表
CREATE TABLE IF NOT EXISTS devices (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(50) NOT NULL UNIQUE,
    name VARCHAR(100) NOT NULL,
    type VARCHAR(50),
    model VARCHAR(50),
    ip VARCHAR(50),
    status VARCHAR(20) DEFAULT 'offline',
    group_id BIGINT UNSIGNED,
    last_online DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_code (code),
    INDEX idx_group_id (group_id),
    INDEX idx_status (status),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 花型文件表
CREATE TABLE IF NOT EXISTS patterns (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    file_name VARCHAR(255) NOT NULL,
    file_path VARCHAR(500),
    file_size BIGINT DEFAULT 0,
    stitches INT DEFAULT 0,
    colors INT DEFAULT 0,
    width DOUBLE DEFAULT 0,
    height DOUBLE DEFAULT 0,
    uploaded_by BIGINT UNSIGNED,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_name (name),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 下发任务表
CREATE TABLE IF NOT EXISTS download_tasks (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    pattern_id BIGINT UNSIGNED NOT NULL,
    device_id BIGINT UNSIGNED NOT NULL,
    status VARCHAR(20) DEFAULT 'waiting',
    progress INT DEFAULT 0,
    message VARCHAR(500),
    operator_id BIGINT UNSIGNED,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_pattern_id (pattern_id),
    INDEX idx_device_id (device_id),
    INDEX idx_status (status),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 员工表
CREATE TABLE IF NOT EXISTS employees (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(50) NOT NULL UNIQUE,
    name VARCHAR(50) NOT NULL,
    department VARCHAR(50),
    position VARCHAR(50),
    phone VARCHAR(20),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_code (code),
    INDEX idx_department (department),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 员工设备绑定表
CREATE TABLE IF NOT EXISTS employee_devices (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    employee_id BIGINT UNSIGNED NOT NULL,
    device_id BIGINT UNSIGNED NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_employee_id (employee_id),
    INDEX idx_device_id (device_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 生产记录表
CREATE TABLE IF NOT EXISTS production_records (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    device_id BIGINT UNSIGNED NOT NULL,
    employee_id BIGINT UNSIGNED,
    pattern_id BIGINT UNSIGNED,
    pieces INT DEFAULT 0,
    stitches BIGINT DEFAULT 0,
    thread_length DOUBLE DEFAULT 0,
    running_time DOUBLE DEFAULT 0,
    idle_time DOUBLE DEFAULT 0,
    record_date DATE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_device_id (device_id),
    INDEX idx_employee_id (employee_id),
    INDEX idx_record_date (record_date),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 报警记录表
CREATE TABLE IF NOT EXISTS alarm_records (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    device_id BIGINT UNSIGNED NOT NULL,
    alarm_type VARCHAR(50),
    alarm_code VARCHAR(20),
    description VARCHAR(500),
    duration INT DEFAULT 0,
    status VARCHAR(20) DEFAULT 'pending',
    start_time DATETIME,
    end_time DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_device_id (device_id),
    INDEX idx_alarm_type (alarm_type),
    INDEX idx_status (status),
    INDEX idx_start_time (start_time),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 工资记录表
CREATE TABLE IF NOT EXISTS salary_records (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    employee_id BIGINT UNSIGNED NOT NULL,
    device_id BIGINT UNSIGNED,
    pieces INT DEFAULT 0,
    unit_price DOUBLE DEFAULT 0,
    salary DOUBLE DEFAULT 0,
    bonus DOUBLE DEFAULT 0,
    total_amount DOUBLE DEFAULT 0,
    record_date DATE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    deleted_at DATETIME,
    INDEX idx_employee_id (employee_id),
    INDEX idx_record_date (record_date),
    INDEX idx_deleted_at (deleted_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 登录记录表
CREATE TABLE IF NOT EXISTS login_logs (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    ip VARCHAR(50),
    device VARCHAR(200),
    status VARCHAR(20),
    login_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_user_id (user_id),
    INDEX idx_login_time (login_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 插入默认管理员用户 (密码: admin123)
INSERT INTO users (username, password, nickname, role) VALUES
('admin', '$2a$10$N9qo8uLOickgx2ZMRZoMye/1qZVJR6jLqJE5fBIVGRV0cTvK7mPGK', '管理员', 'admin')
ON DUPLICATE KEY UPDATE nickname = '管理员';

-- 插入默认设备分组
INSERT INTO device_groups (name, parent_id) VALUES
('全部设备', NULL),
('A车间', 1),
('B车间', 1),
('C车间', 1)
ON DUPLICATE KEY UPDATE name = name;

-- 插入示例设备
INSERT INTO devices (code, name, type, model, ip, status, group_id) VALUES
('A-001', '缝纫机A-001', '缝纫机', 'BM-2000', '192.168.1.101', 'online', 2),
('A-002', '缝纫机A-002', '缝纫机', 'BM-2000', '192.168.1.102', 'working', 2),
('A-003', '缝纫机A-003', '缝纫机', 'BM-3000', '192.168.1.103', 'offline', 2),
('B-001', '缝纫机B-001', '缝纫机', 'BM-3000', '192.168.1.104', 'working', 3),
('B-002', '缝纫机B-002', '缝纫机', 'BM-2000', '192.168.1.105', 'alarm', 3)
ON DUPLICATE KEY UPDATE name = name;

-- 插入示例员工
INSERT INTO employees (code, name, department, position, phone) VALUES
('E001', '张三', '生产部', '操作员', '13800138001'),
('E002', '李四', '生产部', '操作员', '13800138002'),
('E003', '王五', '生产部', '组长', '13800138003'),
('E004', '赵六', '质检部', '质检员', '13800138004'),
('E005', '钱七', '技术部', '工程师', '13800138005')
ON DUPLICATE KEY UPDATE name = name;
