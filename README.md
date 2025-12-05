# DNS 验证服务器 (基于 mysql/mysql.cpp)

本 README 基于项目中的 `mysql/mysql.cpp` 源文件编写，详细描述了该程序的功能、数据库表结构、HTTP 接口、部署与使用说明、已知问题与改进建议。

## 概述

这是一个简单的基于 MySQL 的 DNS 验证服务（HTTP 服务）。主要功能包括：
- 检查请求来源 IP 是否在白名单中
- `verify` 模式：为客户端 IP + domain 在数据库中插入验证记录（dns_verifications）
- `find` 模式：查询之前的验证记录是否存在且未过期，若未过期则返回 domain 对应的目标 IP（从 domain_mappings 表）
- 提供健康检查接口 `/health`

程序内对第三方库做了兼容性处理：若环境缺少 `libmysqlclient`、`jsoncpp` 或 `httplib`，源文件提供了轻量级的 stub，便于在没有依赖时编译或阅读代码逻辑。但在生产环境中应使用正式库以获得完整功能和性能。

---

## 运行环境与依赖

推荐在 Linux 下运行，编译/运行可选方案：

- 推荐（生产）依赖：
  - MySQL 客户端库（libmysqlclient）
  - jsoncpp
  - cpp-httplib（或等效 HTTP 库）
- 可选（测试/阅读）：
  - 源文件内提供了这些库的最小 stub，可在没有安装第三方库时编译并运行（但功能受限或模拟）。

示例编译命令（使用系统库）：
- 使用 g++，假设安装了 mysqlclient 和 jsoncpp：
  g++ mysql/mysql.cpp -o dns_auth_server -lmysqlclient -ljsoncpp -pthread

如果使用自带 stub（不链接外部库），只需：
  g++ mysql/mysql.cpp -o dns_auth_server -pthread

注：根据系统和库的安装位置，实际编译可能需要额外的 include 路径或链接路径（-I/-L）。

---

## 配置

数据库连接按结构体 `DBConfig` 配置（位于源码）：
- host: 默认 "localhost"
- user: 默认 "root"
- password: 默认 "password"
- database: 默认 "dns_auth"
- port: 默认 3306

你可以在源码中直接修改 `DBConfig` 实例，或扩展为读取环境变量 / 配置文件。

程序启动时 (main 函数) 会尝试连接 MySQL 并创建数据库（如果不存在），然后启动 HTTP 服务（默认端口 8080）。

---

## 数据库表结构

程序会创建以下表（若不存在）：

1. ip_whitelist
- id INT AUTO_INCREMENT PRIMARY KEY
- ip VARCHAR(45) NOT NULL UNIQUE
- description VARCHAR(255)
- created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP

用途：存放允许请求的客户端 IP。

2. dns_verifications
- id INT AUTO_INCREMENT PRIMARY KEY
- client_ip VARCHAR(45) NOT NULL
- domain VARCHAR(255) NOT NULL
- expire_time DATETIME NOT NULL
- mode VARCHAR(20) NOT NULL
- created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
- INDEX idx_ip_domain (client_ip, domain)

用途：存放每次 `verify` 模式下的验证记录（到期时间等）。

3. domain_mappings
- id INT AUTO_INCREMENT PRIMARY KEY
- domain VARCHAR(255) NOT NULL UNIQUE
- target_ip VARCHAR(45) NOT NULL
- updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP

用途：域名到目标 IP 的映射表（用于 `find` 模式返回真实 IP）。

4. domain_configs
- id INT AUTO_INCREMENT PRIMARY KEY
- client_ip VARCHAR(45) NOT NULL
- domain VARCHAR(255) NOT NULL
- expire_time DATETIME NOT NULL
- status INT DEFAULT 1
- created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
- UNIQUE KEY unique_ip_domain (client_ip, domain)

用途：配置允许某 client_ip 对某 domain 的授权与到期时间（`verify` 模式会参考此表以获取 expire_time）。

---

## HTTP API

服务器默认监听 `0.0.0.0:8080`（可在 `start` 时传参修改）。

1. POST /dns-auth
- 请求头：
  - 可选 `X-Real-IP`（若存在，程序优先使用它作为客户端 IP），否则使用连接的 remote_addr。
  - Content-Type: application/json (推荐)
- 请求体 JSON（通用字段）：
  - mode: 字符串，必须，取值："verify" 或 "find"
  - 其它字段根据 mode 需求不同

- mode = "verify"
  - 需要字段：
    - domain: 要验证的域名（string）
  - 处理流程：
    1. 检查请求 IP 是否在 ip_whitelist
    2. 在 domain_configs (C 表) 查找 client_ip + domain 且 status = 1，取得 expire_time
    3. 如存在，把一条记录插入 dns_verifications（A 表），mode 字段写 'verify'
    4. 返回 200 与包含 ip/domain/expire_time 的 data
  - 成功响应示例：
    {
      "code": "200",
      "message": "验证成功",
      "data": { "ip": "1.2.3.4", "domain": "example.com", "expire_time": "2025-12-31 23:59:59" }
    }

- mode = "find"
  - 需要字段：
    - ip: 要查询的客户端 IP（string）
    - dn: 要查询的域名（注意字段名在代码中为 "dn"）
  - 处理流程：
    1. 检查请求 IP 是否在 ip_whitelist
    2. 在 dns_verifications (A 表) 查找 client_ip(ip) + domain(dn) 且 mode = 'verify'，读取 expire_time
    3. 检查 expire_time 是否在当前时间之后（未过期）
    4. 若未过期，从 domain_mappings (B 表) 查找 domain 对应的 target_ip
    5. 返回 200 与包含 domain/ip/expire_time 的 data
  - 成功响应示例:
    {
      "code": "200",
      "message": "查询成功",
      "data": { "domain": "example.com", "ip": "5.6.7.8", "expire_time": "2025-12-31 23:59:59" }
    }

- 常见错误与状态：
  - 400: 参数缺失、JSON 解析失败、或不支持的 mode
  - 403: IP 不在白名单 或 验证记录过期
  - 404: 记录不存在（domain_config / dns_verifications / domain_mappings）
  - 500: 数据库插入/查询失败

2. GET /health
- 返回：
  {
    "status": "ok",
    "timestamp": "<unix timestamp>"
  }

---

## 示例请求（curl）

- verify:
  curl -X POST http://127.0.0.1:8080/dns-auth \
    -H "Content-Type: application/json" \
    -d '{"mode":"verify", "domain":"example.com"}'

- find:
  curl -X POST http://127.0.0.1:8080/dns-auth \
    -H "Content-Type: application/json" \
    -d '{"mode":"find", "ip":"1.2.3.4", "dn":"example.com"}'

注意：上述请求需来自在 ip_whitelist 中的 IP，否则会被拒绝。

---

## 辅助脚本 / 操作（源码中提供的函数）

源码中提供了三个方便的辅助函数（可做初始化脚本或手动操作）：

- addIPToWhitelist(conn, ip, description)
  - 将 IP 插入 `ip_whitelist`（使用 INSERT IGNORE）

- addDomainConfig(conn, clientIP, domain, expireTime, status=1)
  - 将或更新 `domain_configs`（ON DUPLICATE KEY UPDATE）

- addDomainMapping(conn, domain, targetIP)
  - 将或更新 `domain_mappings`

示例 SQL（如果你希望用 SQL 手动插入）：
- 插入白名单：
  INSERT INTO ip_whitelist (ip, description) VALUES ('1.2.3.4','测试节点');
- 插入 domain_configs：
  INSERT INTO domain_configs (client_ip, domain, expire_time, status) VALUES ('1.2.3.4','example.com','2025-12-31 23:59:59',1)
    ON DUPLICATE KEY UPDATE expire_time=VALUES(expire_time), status=VALUES(status);
- 插入 domain_mappings：
  INSERT INTO domain_mappings (domain, target_ip) VALUES ('example.com','5.6.7.8') ON DUPLICATE KEY UPDATE target_ip=VALUES(target_ip);

---

## 已知问题与安全注意事项（重要）

1. SQL 注入风险
- 源代码通过字符串拼接构建 SQL（例如 "WHERE ip = '" + ip + "'"），这是高风险做法。强烈建议使用参数化查询 / Prepared Statements（避免任意字符串注入）。

2. 并发与连接池
- 目前每个 `DNSAuthServer` 使用单一 MYSQL* 连接（mysqlConn）。在高并发场景下应使用连接池或对连接断开自动重连的机制。

3. 时间与时区
- 代码使用 `std::get_time` 解析 datetime 字符串并用 `mktime` 转换为 time_t。mktime 会使用系统本地时区，需确认数据库存储时间的时区一致性（推荐使用 UTC 并且在编码/解析时明确时区）。

4. JSON 解析/序列化
- 源码兼容 jsoncpp 库，也提供最小 stub。生产环境应使用成熟 JSON 库以保证边界条件正确处理（例如嵌套对象、数组、转义字符）。

5. 输入校验
- 代码对 domain、ip 等字段只做了空值检查，未校验格式（如合法域名、合法 IP）。建议增加严格校验。

6. 日志与错误处理
- 当前仅使用 stdout/stderr 打印日志。建议引入结构化日志库并区分日志级别（DEBUG/INFO/WARN/ERROR），便于问题定位。

7. HTTP 安全
- 建议启用认证、请求限流、TLS 加密、以及对外网暴露时的额外访问控制。

---

## 建议的改进（TODO）

- 使用 Prepared Statements 修复 SQL 注入。
- 使用连接池（例如自己实现或使用第三方库）以支持并发连接和连接复用。
- 把 DBConfig 改为从环境变量或配置文件读取（避免在源码中硬编码密码）。
- 增加输入格式校验（域名、IP、时间格式）。
- 增加单元测试与集成测试（包括数据库迁移脚本）。
- 支持日志文件与日志轮转。
- 将 HTTP 层替换为生产级别框架，支持中间件（鉴权/限流/审计）。
- 考虑把 JSON 序列化/反序列化逻辑抽象成单独模块，便于替换和单测。

---

## 故障排查

- 无法连接 MySQL：
  - 检查 dbConfig 是否正确，MySQL 是否允许远程连接/防火墙设置
  - 查看 MySQL 日志
- 插入/查询失败：
  - 检查 SQL 是否抛错（程序会打印 mysql_error）
  - 检查表结构
- 时间判断异常（总是过期或从不过期）：
  - 检查数据库中 expire_time 的格式与时区，确认服务器时区

---

## 许可证

请参考仓库根目录的 LICENSE（如果没有请在仓库中添加合适的开源许可证）。

---

如果你希望，我可以：
- 将该 README.md 提交到仓库（我可以帮你生成对应的 commit/PR），
- 或者根据上面的“建议的改进”自动生成 issues/PR 列表并提交（需要你确认目标仓库所有者信息以便进行写操作）。
