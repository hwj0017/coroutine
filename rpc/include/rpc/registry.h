#include <string>
#include <vector>

// 服务节点信息
struct ServiceNode
{
    std::string ip;
    int port;
    int weight = 1;

    // 辅助拼接 "IP:Port"
    std::string to_string() const { return ip + ":" + std::to_string(port); }
};

// 注册中心抽象接口
class Registry
{
  public:
    virtual ~Registry() = default;

    // 注册服务
    virtual bool Register(const std::string& service_name, const ServiceNode& node) = 0;

    // 发现服务
    virtual std::vector<ServiceNode> Discover(const std::string& service_name) = 0;
};