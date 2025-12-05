#include <iostream>
#include <string>
#include <sstream>
#include <ctime>
#include <cstring>
#include <map>
#include <unordered_map>
#include <functional>
#include <iomanip>

// Try to include third-party headers when available; otherwise provide lightweight stubs
#if defined(__has_include)
#  if __has_include(<mysql/mysql.h>)
#    include <mysql/mysql.h>
#  else
// Minimal MySQL stubs so code can compile when libmysqlclient is not available
typedef struct MYSQL { bool connected; } MYSQL;
typedef struct MYSQL_RES { int rows; } MYSQL_RES;
typedef char** MYSQL_ROW;
inline MYSQL* mysql_init(MYSQL* /*unused*/) { MYSQL* m = new MYSQL(); m->connected = false; return m; }
inline MYSQL* mysql_real_connect(MYSQL* conn, const char* host, const char* user, const char* passwd, const char* db, unsigned int /*port*/, const char* /*unix_socket*/, unsigned long /*client_flag*/) {
    if (!conn) return nullptr; conn->connected = true; (void)host; (void)user; (void)passwd; (void)db; return conn;
}
inline void mysql_close(MYSQL* conn) { delete conn; }
inline int mysql_set_character_set(MYSQL* /*conn*/, const char* /*cs*/) { return 0; }
inline const char* mysql_error(MYSQL* /*conn*/) { return "mysql stub"; }
inline int mysql_query(MYSQL* /*conn*/, const char* /*q*/) { return 0; }
inline MYSQL_RES* mysql_store_result(MYSQL* /*conn*/) { return nullptr; }
inline unsigned long mysql_num_rows(MYSQL_RES* /*res*/) { return 0; }
inline void mysql_free_result(MYSQL_RES* /*res*/) { }
inline MYSQL_ROW mysql_fetch_row(MYSQL_RES* /*res*/) { return nullptr; }
#  endif

#  if __has_include(<jsoncpp/json/json.h>)
#    include <jsoncpp/json/json.h>
#  else
// Minimal JSON stub (very small subset used by the program)
namespace Json {
    struct StreamWriterBuilder {};
    struct CharReaderBuilder {};

    class Value {
    public:
        std::string value;
        std::unordered_map<std::string, Value> children;

        Value() = default;
        Value(const std::string& v) : value(v) {}

        bool isMember(const std::string& key) const {
            return children.find(key) != children.end();
        }

        Value& operator[](const std::string& key) {
            return children[key];
        }

        const Value& operator[](const std::string& key) const {
            static Value empty;
            auto it = children.find(key);
            if (it == children.end()) return empty;
            return it->second;
        }

        std::string asString() const { return value; }

        void clear() { value.clear(); children.clear(); }
    };

    inline bool parseFromStream(const CharReaderBuilder& /*b*/, std::istream& is, Value* root, std::string* errs) {
        // very small and lenient parser: accepts flat JSON with string values only
        root->clear();
        std::string s;
        std::getline(is, s, '\0');
        size_t pos = 0;
        while (true) {
            pos = s.find('"', pos);
            if (pos == std::string::npos) break;
            size_t pos2 = s.find('"', pos + 1);
            if (pos2 == std::string::npos) break;
            std::string key = s.substr(pos + 1, pos2 - pos - 1);
            size_t colon = s.find(':', pos2);
            if (colon == std::string::npos) break;
            size_t valStart = s.find('"', colon);
            if (valStart == std::string::npos) break;
            size_t valEnd = s.find('"', valStart + 1);
            if (valEnd == std::string::npos) break;
            std::string val = s.substr(valStart + 1, valEnd - valStart - 1);
            (*root)[key] = Value(val);
            pos = valEnd + 1;
        }
        return true;
    }

    inline std::string writeString(const StreamWriterBuilder& /*b*/, const Value& root) {
        // simple serializer
        // if root has children, serialize them; otherwise return the scalar
        std::ostringstream os;
        bool first = true;
        os << "{";
        for (const auto& kv : root.children) {
            if (!first) os << ",";
            os << "\"" << kv.first << "\":";
            if (!kv.second.children.empty()) {
                os << writeString(StreamWriterBuilder(), kv.second);
            } else {
                os << "\"" << kv.second.value << "\"";
            }
            first = false;
        }
        os << "},";
        if (root.children.empty()) {
            // return scalar
            return root.value;
        }
        return os.str();
    }
}
#  endif

#  if __has_include(<httplib.h>)
#    include <httplib.h>
#  else
// Minimal httplib stub
namespace httplib {
    struct Request {
        std::string remote_addr;
        std::string body;
        std::map<std::string, std::string> headers;
        bool has_header(const std::string& key) const { return headers.find(key) != headers.end(); }
        std::string get_header_value(const std::string& key) const {
            auto it = headers.find(key);
            return it == headers.end() ? std::string() : it->second;
        }
    };

    struct Response {
        std::string content;
        void set_content(const std::string& c, const std::string& /*type*/) { content = c; }
    };

    class Server {
    public:
        using Handler = std::function<void(const Request&, Response&)>;
        void Post(const std::string& /*path*/, const Handler& /*h*/) { }
        void Get(const std::string& /*path*/, const Handler& /*h*/) { }
        void listen(const char* /*host*/, int /*port*/) { }
    };
}
#  endif
#endif

using namespace std;
using namespace httplib;

// MySQL数据库连接配置
struct DBConfig {
    string host = "localhost";
    string user = "root";
    string password = "password";
    string database = "dns_auth";
    int port = 3306;
};

// 响应结构体
struct ResponseStruct {
    int code = 0; // 始终初始化成员变量
    string message;
    string data;
};

class DNSAuthServer {
private:
    DBConfig dbConfig;
    MYSQL* mysqlConn;
    Server server;

public:
    DNSAuthServer() : mysqlConn(nullptr) {}

    ~DNSAuthServer() {
        if (mysqlConn) {
            mysql_close(mysqlConn);
        }
    }

    // 初始化MySQL连接
    bool initMySQL() {
        mysqlConn = mysql_init(nullptr);
        if (!mysqlConn) {
            cerr << "MySQL初始化失败" << endl;
            return false;
        }

        if (!mysql_real_connect(mysqlConn,
            dbConfig.host.c_str(),
            dbConfig.user.c_str(),
            dbConfig.password.c_str(),
            dbConfig.database.c_str(),
            dbConfig.port, nullptr, 0)) {
            cerr << "MySQL连接失败: " << mysql_error(mysqlConn) << endl;
            return false;
        }

        mysql_set_character_set(mysqlConn, "utf8");
        cout << "MySQL连接成功" << endl;

        // 创建必要的表
        createTables();

        return true;
    }

    // 创建数据库表
    void createTables() {
        const char* createTablesSQL[] = {
            // 白名单表
            "CREATE TABLE IF NOT EXISTS ip_whitelist ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "ip VARCHAR(45) NOT NULL UNIQUE,"
            "description VARCHAR(255),"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ")",

            // A表：验证记录表
            "CREATE TABLE IF NOT EXISTS dns_verifications ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "client_ip VARCHAR(45) NOT NULL,"
            "domain VARCHAR(255) NOT NULL,"
            "expire_time DATETIME NOT NULL,"
            "mode VARCHAR(20) NOT NULL,"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "INDEX idx_ip_domain (client_ip, domain)"
            ")",

            // B表：域名-IP映射表
            "CREATE TABLE IF NOT EXISTS domain_mappings ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "domain VARCHAR(255) NOT NULL UNIQUE,"
            "target_ip VARCHAR(45) NOT NULL,"
            "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
            ")",

            // C表：域名配置表
            "CREATE TABLE IF NOT EXISTS domain_configs ("
            "id INT AUTO_INCREMENT PRIMARY KEY,"
            "client_ip VARCHAR(45) NOT NULL,"
            "domain VARCHAR(255) NOT NULL,"
            "expire_time DATETIME NOT NULL,"
            "status INT DEFAULT 1,"
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "UNIQUE KEY unique_ip_domain (client_ip, domain)"
            ")"
        };

        for (const char* sql : createTablesSQL) {
            if (mysql_query(mysqlConn, sql) != 0) {
                cerr << "创建表失败: " << mysql_error(mysqlConn) << endl;
            }
        }
    }

    // 检查IP是否在白名单中
    bool checkIPInWhitelist(const string& ip) {
        string sql = "SELECT id FROM ip_whitelist WHERE ip = '" + ip + "'";

        if (mysql_query(mysqlConn, sql.c_str()) != 0) {
            cerr << "查询白名单失败: " << mysql_error(mysqlConn) << endl;
            return false;
        }

        MYSQL_RES* result = mysql_store_result(mysqlConn);
        if (!result) {
            return false;
        }

        bool exists = (mysql_num_rows(result) > 0);
        mysql_free_result(result);

        return exists;
    }

    // 执行SQL查询
    bool executeSQL(const string& sql) {
        if (mysql_query(mysqlConn, sql.c_str()) != 0) {
            cerr << "SQL执行失败: " << mysql_error(mysqlConn) << endl;
            return false;
        }
        return true;
    }

    // 查询单个结果
    string querySingleValue(const string& sql) {
        if (mysql_query(mysqlConn, sql.c_str()) != 0) {
            return "";
        }

        MYSQL_RES* result = mysql_store_result(mysqlConn);
        if (!result) {
            return "";
        }

        MYSQL_ROW row = mysql_fetch_row(result);
        string value = row ? row[0] : "";

        mysql_free_result(result);
        return value;
    }

    // 验证模式处理
    ResponseStruct handleVerifyMode(const string& clientIP, const Json::Value& jsonData) {
        ResponseStruct response;

        // 获取域名
        if (!jsonData.isMember("domain") || jsonData["domain"].asString().empty()) {
            response.code = 400;
            response.message = "缺少域名参数";
            return response;
        }

        string domain = jsonData["domain"].asString();

        // 从C表查询到期时间
        string sql = "SELECT expire_time FROM domain_configs WHERE client_ip = '" +
            clientIP + "' AND domain = '" + domain + "' AND status = 1";

        string expireTime = querySingleValue(sql);
        if (expireTime.empty()) {
            response.code = 404;
            response.message = "域名配置不存在或已禁用";
            return response;
        }

        // 插入A表记录
        sql = "INSERT INTO dns_verifications (client_ip, domain, expire_time, mode) VALUES ('" +
            clientIP + "', '" + domain + "', '" + expireTime + "', 'verify')";

        if (!executeSQL(sql)) {
            response.code = 500;
            response.message = "数据库插入失败";
            return response;
        }

        response.code = 200;
        response.message = "验证成功";

        Json::Value respJson;
        respJson["ip"] = Json::Value(clientIP);
        respJson["domain"] = Json::Value(domain);
        respJson["expire_time"] = Json::Value(expireTime);

        Json::StreamWriterBuilder writer;
        response.data = Json::writeString(writer, respJson);

        return response;
    }

    // 查找模式处理
    ResponseStruct handleFindMode(const string& clientIP, const Json::Value& jsonData) {
        ResponseStruct response;

        // 验证必要参数
        if (!jsonData.isMember("ip") || jsonData["ip"].asString().empty()) {
            response.code = 400;
            response.message = "缺少IP参数";
            return response;
        }

        if (!jsonData.isMember("dn") || jsonData["dn"].asString().empty()) {
            response.code = 400;
            response.message = "缺少域名参数";
            return response;
        }

        string ip = jsonData["ip"].asString();
        string domain = jsonData["dn"].asString();

        // 在A表中查找对应记录
        string sql = "SELECT expire_time FROM dns_verifications WHERE client_ip = '" +
            ip + "' AND domain = '" + domain + "' AND mode = 'verify'";

        string expireTime = querySingleValue(sql);
        if (expireTime.empty()) {
            response.code = 404;
            response.message = "验证记录不存在";
            return response;
        }

        // 检查是否到期
        time_t now = time(nullptr);
        struct tm expireTm = {};
        {
            std::istringstream ss(expireTime);
            ss >> std::get_time(&expireTm, "%Y-%m-%d %H:%M:%S");
        }
        time_t expireTimestamp = mktime(&expireTm);

        if (now > expireTimestamp) {
            response.code = 403;
            response.message = "域名已过期";
            return response;
        }

        // 从B表查询对应的IP
        sql = "SELECT target_ip FROM domain_mappings WHERE domain = '" + domain + "'";
        string targetIP = querySingleValue(sql);

        if (targetIP.empty()) {
            response.code = 404;
            response.message = "域名映射不存在";
            return response;
        }

        response.code = 200;
        response.message = "查询成功";

        Json::Value respJson;
        respJson["domain"] = Json::Value(domain);
        respJson["ip"] = Json::Value(targetIP);
        respJson["expire_time"] = Json::Value(expireTime);

        Json::StreamWriterBuilder writer;
        response.data = Json::writeString(writer, respJson);

        return response;
    }

    // 处理HTTP POST请求
    void handlePost(const Request& req, Response& res) {
        // 获取客户端IP
        string clientIP = req.has_header("X-Real-IP") ?
            req.get_header_value("X-Real-IP") :
            req.remote_addr;

        cout << "收到请求，客户端IP: " << clientIP << endl;

        // 检查IP白名单
        if (!checkIPInWhitelist(clientIP)) {
            Json::Value errorJson;
            errorJson["code"] = Json::Value(std::to_string(403));
            errorJson["message"] = Json::Value("IP不在白名单中");

            Json::StreamWriterBuilder writer;
            res.set_content(Json::writeString(writer, errorJson), "application/json");
            return;
        }

        // 解析JSON数据
        Json::Value jsonData;
        Json::CharReaderBuilder reader;
        string errors;

        istringstream jsonStream(req.body);
        if (!Json::parseFromStream(reader, jsonStream, &jsonData, &errors)) {
            Json::Value errorJson;
            errorJson["code"] = Json::Value(std::to_string(400));
            errorJson["message"] = Json::Value("JSON解析失败: " + errors);

            Json::StreamWriterBuilder writer;
            res.set_content(Json::writeString(writer, errorJson), "application/json");
            return;
        }

        // 验证mode字段
        if (!jsonData.isMember("mode") || jsonData["mode"].asString().empty()) {
            Json::Value errorJson;
            errorJson["code"] = Json::Value(std::to_string(400));
            errorJson["message"] = Json::Value("缺少mode参数");

            Json::StreamWriterBuilder writer;
            res.set_content(Json::writeString(writer, errorJson), "application/json");
            return;
        }

        string mode = jsonData["mode"].asString();
        ResponseStruct processResponse;

        // 根据mode处理不同请求
        if (mode == "verify") {
            processResponse = handleVerifyMode(clientIP, jsonData);
        }
        else if (mode == "find") {
            processResponse = handleFindMode(clientIP, jsonData);
        }
        else {
            processResponse.code = 400;
            processResponse.message = "不支持的mode类型";
        }

        // 构建响应
        Json::Value respJson;
        respJson["code"] = Json::Value(std::to_string(processResponse.code));
        respJson["message"] = Json::Value(processResponse.message);

        if (!processResponse.data.empty()) {
            Json::Value dataJson;
            Json::CharReaderBuilder dataReader;
            istringstream dataStream(processResponse.data);
            if (Json::parseFromStream(dataReader, dataStream, &dataJson, &errors)) {
                respJson["data"] = dataJson;
            }
        }

        Json::StreamWriterBuilder writer;
        res.set_content(Json::writeString(writer, respJson), "application/json");
    }

    // 启动服务器
    void start(int port = 8080) {
        if (!initMySQL()) {
            cerr << "MySQL初始化失败，服务器启动中止" << endl;
            return;
        }

        server.Post("/dns-auth", [this](const Request& req, Response& res) {
            this->handlePost(req, res);
            });

        server.Get("/health", [](const Request& req, Response& res) {
            Json::Value healthJson;
            healthJson["status"] = Json::Value("ok");
            healthJson["timestamp"] = Json::Value(std::to_string(static_cast<int>(time(nullptr))));

            Json::StreamWriterBuilder writer;
            res.set_content(Json::writeString(writer, healthJson), "application/json");
            });

        cout << "DNS验证服务器启动，监听端口: " << port << endl;
        server.listen("0.0.0.0", port);
    }
};

// 辅助函数：添加IP到白名单
void addIPToWhitelist(MYSQL* conn, const string& ip, const string& description = "") {
    string sql = "INSERT IGNORE INTO ip_whitelist (ip, description) VALUES ('" +
        ip + "', '" + description + "')";

    if (mysql_query(conn, sql.c_str()) == 0) {
        cout << "IP " << ip << " 已添加到白名单" << endl;
    }
    else {
        cerr << "添加白名单失败: " << mysql_error(conn) << endl;
    }
}

// 辅助函数：添加域名配置
void addDomainConfig(MYSQL* conn, const string& clientIP, const string& domain,
    const string& expireTime, int status = 1) {
    string sql = "INSERT INTO domain_configs (client_ip, domain, expire_time, status) VALUES ('" +
        clientIP + "', '" + domain + "', '" + expireTime + "', " + to_string(status) +
        ") ON DUPLICATE KEY UPDATE expire_time = VALUES(expire_time), status = VALUES(status)";

    if (mysql_query(conn, sql.c_str()) == 0) {
        cout << "域名配置已添加/更新: " << domain << " -> " << clientIP << endl;
    }
    else {
        cerr << "添加域名配置失败: " << mysql_error(conn) << endl;
    }
}

// 辅助函数：添加域名映射
void addDomainMapping(MYSQL* conn, const string& domain, const string& targetIP) {
    string sql = "INSERT INTO domain_mappings (domain, target_ip) VALUES ('" +
        domain + "', '" + targetIP + "') ON DUPLICATE KEY UPDATE target_ip = VALUES(target_ip)";

    if (mysql_query(conn, sql.c_str()) == 0) {
        cout << "域名映射已添加/更新: " << domain << " -> " << targetIP << endl;
    }
    else {
        cerr << "添加域名映射失败: " << mysql_error(conn) << endl;
    }
}

int main() {
    // 数据库配置
    DBConfig dbConfig;
    // 可以根据需要修改配置
    // dbConfig.host = "127.0.0.1";
    // dbConfig.user = "your_username";
    // dbConfig.password = "your_password";

    // 初始化数据库（一次性操作）
    MYSQL* initConn = mysql_init(nullptr);
    if (initConn && mysql_real_connect(initConn, dbConfig.host.c_str(),
        dbConfig.user.c_str(), dbConfig.password.c_str(),
        nullptr, dbConfig.port, nullptr, 0)) {

        // 创建数据库
        if (mysql_query(initConn, ("CREATE DATABASE IF NOT EXISTS " + dbConfig.database).c_str()) == 0) {
            cout << "数据库创建/验证成功" << endl;
        }

        mysql_close(initConn);
    }

    // 创建并启动DNS验证服务器
    DNSAuthServer server;
    server.start(8080);

    return 0;
}