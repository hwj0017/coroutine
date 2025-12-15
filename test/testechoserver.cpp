#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CONNECTIONS 1024
#define BUFFER_SIZE 4096

// 客户端连接上下文
struct conn
{
    int fd;
    int state; // 0: waiting for recv, 1: waiting for send
    char buffer[BUFFER_SIZE];
};

static struct conn connections[MAX_CONNECTIONS] = {0};

// 设置 socket 为非阻塞
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // 创建监听 socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) < 0)
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    set_nonblocking(listen_fd);
    printf("Echo server listening on port %d\n", port);

    // 初始化 io_uring（预留足够 SQE）
    struct io_uring ring;
    if (io_uring_queue_init(2048, &ring, 0) < 0)
    {
        perror("io_uring_queue_init");
        close(listen_fd);
        return 1;
    }

    // 提交初始 accept
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
    sqe->user_data = 0; // 0 表示 accept 请求
    io_uring_submit(&ring);

    struct io_uring_cqe* cqe;
    while (1)
    {
        // 非阻塞轮询完成项
        int ret = io_uring_peek_cqe(&ring, &cqe);
        if (ret == -EAGAIN)
        {
            // 无完成项，短暂休眠避免 CPU 100%
            usleep(1000);
            continue;
        }

        if (ret != 0)
        {
            fprintf(stderr, "io_uring_peek_cqe: %s\n", strerror(-ret));
            break;
        }

        uint64_t user_data = cqe->user_data;
        int res = cqe->res;

        if (user_data == 0)
        {
            // accept 完成
            if (res >= 0)
            {
                int client_fd = res;
                set_nonblocking(client_fd);
                printf("New connection: %d\n", client_fd);

                // 找到空闲连接槽
                int conn_id = -1;
                for (int i = 0; i < MAX_CONNECTIONS; i++)
                {
                    if (connections[i].fd == 0)
                    {
                        conn_id = i;
                        break;
                    }
                }

                if (conn_id >= 0)
                {
                    connections[conn_id].fd = client_fd;
                    connections[conn_id].state = 0; // 等待 recv

                    // 提交 recv 请求
                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, client_fd, connections[conn_id].buffer, BUFFER_SIZE, 0);
                    sqe->user_data = conn_id + 1; // +1 避免与 accept 冲突
                    io_uring_submit(&ring);
                }
                else
                {
                    close(client_fd);
                    fprintf(stderr, "Too many connections\n");
                }
            }

            // 重新提交 accept
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_accept(sqe, listen_fd, NULL, NULL, 0);
            sqe->user_data = 0;
            io_uring_submit(&ring);
        }
        else
        {
            // 客户端 I/O 完成
            int conn_id = user_data - 1;
            if (conn_id < 0 || conn_id >= MAX_CONNECTIONS || connections[conn_id].fd == 0)
            {
                // 无效连接
                io_uring_cqe_seen(&ring, cqe);
                continue;
            }

            struct conn* conn = &connections[conn_id];

            if (res <= 0)
            {
                // 连接关闭或错误
                printf("Connection closed: %d\n", conn->fd);
                close(conn->fd);
                conn->fd = 0;
            }
            else
            {
                if (conn->state == 0)
                {
                    // recv 完成，准备 echo
                    int nread = res;
                    // 提交 send 请求（echo）
                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_send(sqe, conn->fd, conn->buffer, nread, 0);
                    conn->state = 1; // 等待 send
                    sqe->user_data = user_data;
                    io_uring_submit(&ring);
                }
                else
                {
                    // send 完成，重新提交 recv
                    conn->state = 0;
                    sqe = io_uring_get_sqe(&ring);
                    io_uring_prep_recv(sqe, conn->fd, conn->buffer, BUFFER_SIZE, 0);
                    sqe->user_data = user_data;
                    io_uring_submit(&ring);
                }
            }
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    close(listen_fd);
    return 0;
}