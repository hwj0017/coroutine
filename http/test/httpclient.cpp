
#include <arpa/inet.h>
#include <assert.h>
#include <cstdio>
#include <iostream>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
constexpr int BUFFER_SIZE = 1000;
int main()
{
    // 创建套接子
    // AF_INFT表示ipv4  SOCKET_STREAM表示传输层使用tcp协议
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sockfd != -1);
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(8888);
    saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int res = connect(sockfd, (struct sockaddr*)&saddr, sizeof(saddr));
    assert(res != -1);
    std::string str = "GET / HTTP/1.0\r\nHost : 127.0.0.1 : 1234\r\nConnection : Keep -alive\r\n"
                      "Content -Length : "
                      "12\r\n\r\nhello world";
    char buf[BUFFER_SIZE];
    // std::cin >> str;
    int readNum = send(sockfd, str.data(), str.size(), 0);
    if (readNum == -1)
    {
        return 0;
    }

    int writeNum = recv(sockfd, buf, BUFFER_SIZE, 0);
    if (writeNum <= 0)
    {
        return 0;
    }
    std::cout << writeNum << std::endl;
    std::cout << buf << std::endl;
    close(sockfd);

    exit(0);
}