#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PROXY_PORT 8080
#define SERVER_PORT 8000
#define SERVER_HOST "127.0.0.1"
#define BUFFER_SIZE 4096

int main() {
    int proxy_fd, client_fd, server_fd;
    struct sockaddr_in proxy_addr, client_addr, server_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // 创建代理服务器监听套接字
    proxy_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_fd < 0) {
        perror("socket 创建失败");
        exit(EXIT_FAILURE);
    }

    // 配置代理服务器地址
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(PROXY_PORT);

    // 绑定套接字
    if (bind(proxy_fd, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0) {
        perror("bind 失败");
        exit(EXIT_FAILURE);
    }

    // 监听连接
    if (listen(proxy_fd, 10) < 0) {
        perror("listen 失败");
        exit(EXIT_FAILURE);
    }

    printf("代理服务器 (版本 v4 - 恢复请求转发 + 流式) 正在监听端口 %d, 并转发到 http://%s:%d\n", PROXY_PORT, SERVER_HOST, SERVER_PORT);

    while (1) {
        client_fd = accept(proxy_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("accept 失败");
            continue;
        }
        printf("接受来自 %s:%d 的连接\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("服务器 socket 创建失败");
            close(client_fd);
            continue;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, SERVER_HOST, &server_addr.sin_addr) <= 0) {
            perror("无效的服务器地址");
            close(client_fd);
            close(server_fd);
            continue;
        }

        if (connect(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("连接到服务器失败");
            close(client_fd);
            close(server_fd);
            continue;
        }
        printf("已连接到服务器 %s:%d\n", SERVER_HOST, SERVER_PORT);

        // --- 恢复客户端请求转发 ---
        while (1) {
            bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
            if (bytes_received < 0) {
                perror("从客户端接收数据失败");
                printf("errno: %d, strerror: %s\n", errno, strerror(errno));
                break;
            } else if (bytes_received == 0) {
                printf("客户端关闭连接，停止转发客户端数据\n");
                break; // 客户端关闭连接
            } else {
                printf("从客户端接收到 %zd 字节，转发到服务器\n", bytes_received); // 【保留日志】
                if (send(server_fd, buffer, bytes_received, 0) < 0) {
                    perror("发送到服务器失败");
                    break;
                }
                // 成功转发客户端数据后，跳出内层循环，开始接收服务器数据
                break; // **关键修改：转发一次客户端请求后跳出内层循环**
            }
        }


        // --- 流式转发服务器响应 ---
        while (1) {
            printf("准备从服务器接收数据...\n"); // 【保留日志】
            bytes_received = recv(server_fd, buffer, BUFFER_SIZE, 0);
            printf("recv() 返回值: %zd\n", bytes_received); // 【保留日志】

            if (bytes_received < 0) {
                perror("从服务器接收数据失败");
                printf("errno: %d, strerror: %s\n", errno, strerror(errno));
                break;
            } else if (bytes_received == 0) {
                printf("服务器关闭连接或无数据，停止转发服务器数据\n");
                break; // 服务器关闭连接或没有更多数据
            } else {
                printf("从服务器接收到 %zd 字节，转发到客户端\n", bytes_received); // 【保留日志】
                if (send(client_fd, buffer, bytes_received, 0) < 0) {
                    perror("发送到客户端失败");
                    break;
                }
            }
        }

        close(client_fd);
        close(server_fd);
        printf("关闭客户端和服务器连接\n");
    }

    close(proxy_fd);
    return 0;
}