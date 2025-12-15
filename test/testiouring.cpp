#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main()
{
    // 1. 创建 io_uring（队列深度=1）
    struct io_uring ring;
    io_uring_queue_init(1, &ring, 0);

    // 2. 打开文件（必须用 O_DIRECT 或普通文件）
    int fd = open("test.txt", O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    // 3. 准备缓冲区
    char buffer[4096];
    struct io_uring_sqe* sqe;

    // 4. 获取 SQE
    sqe = io_uring_get_sqe(&ring);
    // 设置 read 请求
    io_uring_prep_read(sqe, fd, buffer, sizeof(buffer), 0);
    // 关联用户数据（可选）
    sqe->user_data = 123;

    // 5. 提交请求
    io_uring_submit(&ring);

    // 6. 等待完成
    struct io_uring_cqe* cqe;
    io_uring_wait_cqe(&ring, &cqe);

    // 7. 处理结果
    if (cqe->res < 0)
    {
        fprintf(stderr, "Read failed: %s\n", strerror(-cqe->res));
    }
    else
    {
        printf("Read %d bytes: %.20s...\n", cqe->res, buffer);
    }

    // 8. 标记 CQE 已处理
    io_uring_cqe_seen(&ring, cqe);

    // 清理
    close(fd);
    io_uring_queue_exit(&ring);
    return 0;
}