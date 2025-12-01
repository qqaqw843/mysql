#pragma#ifndef DATABASEHELPER_H
#define DATABASEHELPER_H

#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>
#include <string>
#include <vector>
#include <memory>
#include <ctime>

// 用户结构体
struct User {
    int id;
    std::string username;
    std::string email;
    std::string role;
    std::string status;
    std::time_t created_at;
};

// 节点结构体
struct Node {
    int id;
    std::string name;
    std::string host;
    int port;
    std::string region;
    std::string status;
    int max_connections;
    int current_connections;
    long long bandwidth_limit;
    std::string description;
    std::time_t created_at;
};

// 隧道结构体
struct Tunnel {
    int id;
    int user_id;
    int node_id;
    std::string name;
    std::string protocol;
    std::string local_ip;
    int local_port;
    int remote_port;
    std::string custom_domain;
    std::string status;
    long long bandwidth_used;
    std::time_t last_connected;
    std::time_t created_at;
    std::time_t updated_at;
};

// 使用记录结构体
struct UsageRecord {
    long long id;
    int user_id;
    int tunnel_id;
    int node_id;
    long long bytes_sent;
    long long bytes_received;
    int connection_time;
    std::string record_date;
    std::time_t created_at;
};

class DatabaseHelper {
private:
    std::unique_ptr<sql::mysql::MySQL_Driver> driver;
    std::unique_ptr<sql::Connection> connection;
    std::string connectionString;

    void initializeDatabase();
    std::time_t stringToTime(const std::string& timeStr);
    std::string timeToString(std::time_t time);

public:
    DatabaseHelper(const std::string& host, const std::string& user,
        const std::string& password, const std::string& database);
    ~DatabaseHelper();

    // 连接管理
    bool connect();
    void disconnect();
    bool isConnected();

    // 用户操作
    std::unique_ptr<User> authenticateUser(const std::string& username, const std::string& password);
    bool createUser(const std::string& username, const std::string& email,
        const std::string& passwordHash, const std::string& role = "user");
    std::unique_ptr<User> getUserById(int userId);
    bool updateUserStatus(int userId, const std::string& status);

    // 节点操作
    std::vector<Node> getAvailableNodes();
    std::unique_ptr<Node> getNodeById(int nodeId);
    bool updateNodeStatus(int nodeId, const std::string& status);
    bool updateNodeConnections(int nodeId, int currentConnections);

    // 隧道操作
    std::vector<Tunnel> getUserTunnels(int userId);
    std::unique_ptr<Tunnel> getTunnelById(int tunnelId);
    int createTunnel(const Tunnel& tunnel);
    bool updateTunnelStatus(int tunnelId, const std::string& status);
    bool updateTunnelBandwidth(int tunnelId, long long bandwidthUsed);
    bool updateTunnelLastConnected(int tunnelId);
    bool deleteTunnel(int tunnelId);

    // 使用记录操作
    void recordUsage(int userId, int tunnelId, int nodeId,
        long long bytesSent, long long bytesReceived, int connectionTime);
    std::vector<UsageRecord> getUserUsage(int userId, const std::string& startDate, const std::string& endDate);

    // 系统配置操作
    std::string getSystemConfig(const std::string& configKey);
    bool setSystemConfig(const std::string& configKey, const std::string& configValue);

    // 工具函数
    int getNextAvailableRemotePort(int nodeId);
    int getUserTunnelCount(int userId);
};

#endif // DATABASEHELPER_H once
