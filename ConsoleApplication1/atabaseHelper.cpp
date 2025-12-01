#include "DatabaseHelper.h"
#include <iostream>
#include <sstream>
#include <iomanip>

DatabaseHelper::DatabaseHelper(const std::string& host, const std::string& user,
    const std::string& password, const std::string& database) {
    try {
        driver = std::unique_ptr<sql::mysql::MySQL_Driver>(sql::mysql::get_mysql_driver_instance());
        connectionString = "tcp://" + host + ":3306";

        connection = std::unique_ptr<sql::Connection>(
            driver->connect(connectionString, user, password)
        );
        connection->setSchema(database);

        std::cout << "数据库连接成功!" << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cerr << "数据库连接错误: " << e.what() << std::endl;
        std::cerr << "MySQL错误代码: " << e.getErrorCode() << std::endl;
    }
}

DatabaseHelper::~DatabaseHelper() {
    disconnect();
}

bool DatabaseHelper::connect() {
    try {
        if (!connection->isValid() || connection->isClosed()) {
            connection->reconnect();
        }
        return true;
    }
    catch (sql::SQLException& e) {
        std::cerr << "数据库重连错误: " << e.what() << std::endl;
        return false;
    }
}

void DatabaseHelper::disconnect() {
    if (connection && !connection->isClosed()) {
        connection->close();
    }
}

bool DatabaseHelper::isConnected() {
    return connection && connection->isValid() && !connection->isClosed();
}

std::time_t DatabaseHelper::stringToTime(const std::string& timeStr) {
    std::tm tm = {};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    return std::mktime(&tm);
}

std::string DatabaseHelper::timeToString(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// 用户认证
std::unique_ptr<User> DatabaseHelper::authenticateUser(const std::string& username, const std::string& password) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "SELECT id, username, email, role, status, created_at FROM users "
                "WHERE (username = ? OR email = ?) AND status = 'active'"
            )
        );
        pstmt->setString(1, username);
        pstmt->setString(2, username);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next()) {
            // 这里应该验证密码哈希，简化示例
            auto user = std::make_unique<User>();
            user->id = res->getInt("id");
            user->username = res->getString("username");
            user->email = res->getString("email");
            user->role = res->getString("role");
            user->status = res->getString("status");
            user->created_at = stringToTime(res->getString("created_at"));

            return user;
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "用户认证错误: " << e.what() << std::endl;
    }

    return nullptr;
}

// 创建用户
bool DatabaseHelper::createUser(const std::string& username, const std::string& email,
    const std::string& passwordHash, const std::string& role) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "INSERT INTO users (username, email, password_hash, role) VALUES (?, ?, ?, ?)"
            )
        );
        pstmt->setString(1, username);
        pstmt->setString(2, email);
        pstmt->setString(3, passwordHash);
        pstmt->setString(4, role);

        return pstmt->executeUpdate() > 0;
    }
    catch (sql::SQLException& e) {
        std::cerr << "创建用户错误: " << e.what() << std::endl;
        return false;
    }
}

// 获取用户的所有隧道
std::vector<Tunnel> DatabaseHelper::getUserTunnels(int userId) {
    std::vector<Tunnel> tunnels;

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "SELECT * FROM tunnels WHERE user_id = ? ORDER BY created_at DESC"
            )
        );
        pstmt->setInt(1, userId);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            Tunnel tunnel;
            tunnel.id = res->getInt("id");
            tunnel.user_id = res->getInt("user_id");
            tunnel.node_id = res->getInt("node_id");
            tunnel.name = res->getString("name");
            tunnel.protocol = res->getString("protocol");
            tunnel.local_ip = res->getString("local_ip");
            tunnel.local_port = res->getInt("local_port");
            tunnel.remote_port = res->getInt("remote_port");

            if (res->isNull("custom_domain")) {
                tunnel.custom_domain = "";
            }
            else {
                tunnel.custom_domain = res->getString("custom_domain");
            }

            tunnel.status = res->getString("status");
            tunnel.bandwidth_used = res->getInt64("bandwidth_used");

            if (!res->isNull("last_connected")) {
                tunnel.last_connected = stringToTime(res->getString("last_connected"));
            }

            tunnel.created_at = stringToTime(res->getString("created_at"));
            tunnel.updated_at = stringToTime(res->getString("updated_at"));

            tunnels.push_back(tunnel);
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "获取用户隧道错误: " << e.what() << std::endl;
    }

    return tunnels;
}

// 创建新隧道
int DatabaseHelper::createTunnel(const Tunnel& tunnel) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "INSERT INTO tunnels (user_id, node_id, name, protocol, local_ip, local_port, remote_port, custom_domain) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
            )
        );
        pstmt->setInt(1, tunnel.user_id);
        pstmt->setInt(2, tunnel.node_id);
        pstmt->setString(3, tunnel.name);
        pstmt->setString(4, tunnel.protocol);
        pstmt->setString(5, tunnel.local_ip);
        pstmt->setInt(6, tunnel.local_port);

        if (tunnel.remote_port > 0) {
            pstmt->setInt(7, tunnel.remote_port);
        }
        else {
            pstmt->setNull(7, sql::DataType::INTEGER);
        }

        if (!tunnel.custom_domain.empty()) {
            pstmt->setString(8, tunnel.custom_domain);
        }
        else {
            pstmt->setNull(8, sql::DataType::VARCHAR);
        }

        pstmt->executeUpdate();

        // 获取最后插入的ID
        std::unique_ptr<sql::Statement> stmt(connection->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));

        if (res->next()) {
            return res->getInt(1);
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "创建隧道错误: " << e.what() << std::endl;
    }

    return -1;
}

// 更新隧道状态
bool DatabaseHelper::updateTunnelStatus(int tunnelId, const std::string& status) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "UPDATE tunnels SET status = ?, updated_at = NOW() WHERE id = ?"
            )
        );
        pstmt->setString(1, status);
        pstmt->setInt(2, tunnelId);

        return pstmt->executeUpdate() > 0;
    }
    catch (sql::SQLException& e) {
        std::cerr << "更新隧道状态错误: " << e.what() << std::endl;
        return false;
    }
}

// 获取可用节点
std::vector<Node> DatabaseHelper::getAvailableNodes() {
    std::vector<Node> nodes;

    try {
        std::unique_ptr<sql::Statement> stmt(connection->createStatement());
        std::unique_ptr<sql::ResultSet> res(
            stmt->executeQuery("SELECT * FROM nodes WHERE status = 'online' ORDER BY region, name")
        );

        while (res->next()) {
            Node node;
            node.id = res->getInt("id");
            node.name = res->getString("name");
            node.host = res->getString("host");
            node.port = res->getInt("port");
            node.region = res->getString("region");
            node.status = res->getString("status");
            node.max_connections = res->getInt("max_connections");
            node.current_connections = res->getInt("current_connections");
            node.bandwidth_limit = res->getInt64("bandwidth_limit");
            node.description = res->getString("description");
            node.created_at = stringToTime(res->getString("created_at"));

            nodes.push_back(node);
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "获取可用节点错误: " << e.what() << std::endl;
    }

    return nodes;
}

// 记录使用情况
void DatabaseHelper::recordUsage(int userId, int tunnelId, int nodeId,
    long long bytesSent, long long bytesReceived, int connectionTime) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "INSERT INTO usage_records "
                "(user_id, tunnel_id, node_id, bytes_sent, bytes_received, connection_time, record_date) "
                "VALUES (?, ?, ?, ?, ?, ?, CURDATE())"
            )
        );
        pstmt->setInt(1, userId);
        pstmt->setInt(2, tunnelId);
        pstmt->setInt(3, nodeId);
        pstmt->setInt64(4, bytesSent);
        pstmt->setInt64(5, bytesReceived);
        pstmt->setInt(6, connectionTime);

        pstmt->executeUpdate();
    }
    catch (sql::SQLException& e) {
        std::cerr << "记录使用情况错误: " << e.what() << std::endl;
    }
}

// 获取系统配置
std::string DatabaseHelper::getSystemConfig(const std::string& configKey) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "SELECT config_value FROM system_config WHERE config_key = ?"
            )
        );
        pstmt->setString(1, configKey);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next()) {
            return res->getString("config_value");
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "获取系统配置错误: " << e.what() << std::endl;
    }

    return "";
}

// 获取下一个可用远程端口
int DatabaseHelper::getNextAvailableRemotePort(int nodeId) {
    try {
        // 获取端口范围配置
        int startPort = std::stoi(getSystemConfig("default_remote_port_start"));
        int endPort = std::stoi(getSystemConfig("default_remote_port_end"));

        // 查找已使用的端口
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "SELECT remote_port FROM tunnels WHERE node_id = ? AND remote_port IS NOT NULL"
            )
        );
        pstmt->setInt(1, nodeId);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        std::vector<int> usedPorts;

        while (res->next()) {
            usedPorts.push_back(res->getInt("remote_port"));
        }

        // 寻找可用端口
        for (int port = startPort; port <= endPort; port++) {
            if (std::find(usedPorts.begin(), usedPorts.end(), port) == usedPorts.end()) {
                return port;
            }
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "获取可用端口错误: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "端口分配错误: " << e.what() << std::endl;
    }

    return -1;
}

// 获取用户隧道数量
int DatabaseHelper::getUserTunnelCount(int userId) {
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            connection->prepareStatement(
                "SELECT COUNT(*) as count FROM tunnels WHERE user_id = ?"
            )
        );
        pstmt->setInt(1, userId);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next()) {
            return res->getInt("count");
        }
    }
    catch (sql::SQLException& e) {
        std::cerr << "获取用户隧道数量错误: " << e.what() << std::endl;
    }

    return 0;
}