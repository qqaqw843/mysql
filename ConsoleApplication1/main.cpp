#include "DatabaseHelper.h"
#include <iostream>
#include <iomanip>

void printTunnelInfo(const Tunnel& tunnel) {
    std::cout << "隧道ID: " << tunnel.id << std::endl;
    std::cout << "名称: " << tunnel.name << std::endl;
    std::cout << "协议: " << tunnel.protocol << std::endl;
    std::cout << "本地地址: " << tunnel.local_ip << ":" << tunnel.local_port << std::endl;
    std::cout << "远程端口: " << tunnel.remote_port << std::endl;
    std::cout << "状态: " << tunnel.status << std::endl;
    std::cout << "------------------------" << std::endl;
}

void printNodeInfo(const Node& node) {
    std::cout << "节点ID: " << node.id << std::endl;
    std::cout << "名称: " << node.name << std::endl;
    std::cout << "地址: " << node.host << ":" << node.port << std::endl;
    std::cout << "地区: " << node.region << std::endl;
    std::cout << "状态: " << node.status << std::endl;
    std::cout << "连接数: " << node.current_connections << "/" << node.max_connections << std::endl;
    std::cout << "------------------------" << std::endl;
}

int main() {
    // 初始化数据库连接
    DatabaseHelper db("localhost", "root", "password", "frp_manager");

    if (!db.isConnected()) {
        std::cerr << "数据库连接失败!" << std::endl;
        return -1;
    }

    // 用户认证
    auto user = db.authenticateUser("admin", "password");
    if (user) {
        std::cout << "用户认证成功: " << user->username << std::endl;

        // 获取用户隧道
        auto tunnels = db.getUserTunnels(user->id);
        std::cout << "\n用户隧道列表:" << std::endl;
        for (const auto& tunnel : tunnels) {
            printTunnelInfo(tunnel);
        }

        // 获取可用节点
        auto nodes = db.getAvailableNodes();
        std::cout << "\n可用节点列表:" << std::endl;
        for (const auto& node : nodes) {
            printNodeInfo(node);
        }

        if (!nodes.empty()) {
            // 创建新隧道
            Tunnel newTunnel;
            newTunnel.user_id = user->id;
            newTunnel.node_id = nodes[0].id;
            newTunnel.name = "Web服务";
            newTunnel.protocol = "tcp";
            newTunnel.local_ip = "127.0.0.1";
            newTunnel.local_port = 80;
            newTunnel.remote_port = db.getNextAvailableRemotePort(nodes[0].id);

            int tunnelId = db.createTunnel(newTunnel);
            if (tunnelId > 0) {
                std::cout << "隧道创建成功，ID: " << tunnelId << std::endl;

                // 记录使用情况
                db.recordUsage(user->id, tunnelId, nodes[0].id, 1024 * 1024, 512 * 1024, 3600);
                std::cout << "使用记录已添加" << std::endl;
            }
        }

        // 获取系统配置
        auto maxTunnels = db.getSystemConfig("max_tunnels_per_user");
        std::cout << "\n每个用户最大隧道数: " << maxTunnels << std::endl;

        // 获取用户当前隧道数
        int tunnelCount = db.getUserTunnelCount(user->id);
        std::cout << "用户当前隧道数: " << tunnelCount << std::endl;
    }
    else {
        std::cout << "用户认证失败!" << std::endl;
    }

    return 0;
}次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
