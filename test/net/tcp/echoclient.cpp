// echo_client.cpp
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

int main(int argc, char* argv[])
{
    const char* host = "127.0.0.1";
    int port = 8080;

    // 允许命令行指定地址和端口: ./client <host> <port>
    if (argc >= 2)
    {
        host = argv[1];
    }
    if (argc >= 3)
    {
        port = atoi(argv[2]);
    }

    // 创建 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        throw runtime_error("Failed to create socket");
    }

    // 设置地址
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0)
    {
        close(sockfd);
        throw runtime_error("Invalid address");
    }

    // 连接服务器
    if (connect(sockfd, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        close(sockfd);
        throw runtime_error("Connection failed");
    }

    cout << "Connected to " << host << ":" << port << endl;
    cout << "Enter message (type 'quit' to exit):" << endl;

    string line;
    char buffer[1024];

    while (getline(cin, line))
    {
        if (line == "quit")
            break;

        // 发送数据
        if (send(sockfd, line.c_str(), line.size(), 0) < 0)
        {
            cerr << "Send failed" << endl;
            break;
        }

        // 接收回显（简单：读一次，假设服务器一次返回完整数据）
        ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0)
        {
            cerr << "Server closed connection or error" << endl;
            break;
        }
        buffer[n] = '\0';
        cout << "Echo: " << buffer << endl;
    }

    close(sockfd);
    cout << "Disconnected." << endl;
    return 0;
}