-- 创建数据库
CREATE DATABASE IF NOT EXISTS dns_auth;
USE dns_auth;

-- 白名单表
CREATE TABLE IF NOT EXISTS ip_whitelist (
    id INT AUTO_INCREMENT PRIMARY KEY,
    ip VARCHAR(45) NOT NULL UNIQUE,
    description VARCHAR(255),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 验证记录表
CREATE TABLE IF NOT EXISTS dns_verifications (
    id INT AUTO_INCREMENT PRIMARY KEY,
    client_ip VARCHAR(45) NOT NULL,
    domain VARCHAR(255) NOT NULL,
    expire_time DATETIME NOT NULL,
    mode VARCHAR(20) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_ip_domain (client_ip, domain)
);

-- 域名-IP映射表
CREATE TABLE IF NOT EXISTS domain_mappings (
    id INT AUTO_INCREMENT PRIMARY KEY,
    domain VARCHAR(255) NOT NULL UNIQUE,
    target_ip VARCHAR(45) NOT NULL,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 域名配置表
CREATE TABLE IF NOT EXISTS domain_configs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    client_ip VARCHAR(45) NOT NULL,
    domain VARCHAR(255) NOT NULL,
    expire_time DATETIME NOT NULL,
    status INT DEFAULT 1,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY unique_ip_domain (client_ip, domain)
);

-- 插入示例数据
INSERT INTO ip_whitelist (ip, description) VALUES 
('192.168.1.100', '内部测试客户端'),
('10.0.0.5', '生产环境客户端');

INSERT INTO domain_configs (client_ip, domain, expire_time, status) VALUES
('192.168.1.100', 'test.example.com', '2024-12-31 23:59:59', 1),
('10.0.0.5', 'prod.example.com', '2024-12-31 23:59:59', 1);

INSERT INTO domain_mappings (domain, target_ip) VALUES
('test.example.com', '192.168.1.100'),
('prod.example.com', '10.0.0.5');