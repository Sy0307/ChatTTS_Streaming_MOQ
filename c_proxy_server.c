#include <event2/event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BACKEND_SERVER_URL "http://localhost:8000" // 修改为你的 Python Flask API 地址

// curl 回调函数声明 (避免隐式声明警告)
size_t proxy_curl_write_callback(char *buffer, size_t size, size_t nmemb, void *userp);
size_t proxy_curl_header_callback(char *buffer, size_t size, size_t nmemb, void *userp);

void http_handler(struct evhttp_request *req, void *arg) {
    printf("进入 http_handler 函数\n"); // 添加日志

    struct evbuffer *client_output_buffer = evhttp_request_get_output_buffer(req);
    if (!client_output_buffer) {
        fprintf(stderr, "输出缓冲区创建失败\n"); // 使用 fprintf 输出到标准错误
        evhttp_send_error(req, 503, "输出缓冲区创建失败");
        printf("http_handler: evhttp_send_error (缓冲区失败) 发送完成\n"); // 添加日志
        return;
    }
    printf("http_handler: 获取 client_output_buffer 成功\n"); // 添加日志

    CURLM *multi_handle = curl_multi_init();
    CURL *easy_handle = curl_easy_init();
    if (!easy_handle || !multi_handle) {
        fprintf(stderr, "curl 初始化失败\n"); // 使用 fprintf 输出到标准错误
        evhttp_send_error(req, 503, "curl 初始化失败");
        printf("http_handler: evhttp_send_error (curl 初始化失败) 发送完成\n"); // 添加日志
        curl_multi_cleanup(multi_handle);
        curl_easy_cleanup(easy_handle);
        return;
    }
    printf("http_handler: curl 初始化成功\n"); // 添加日志

    curl_easy_setopt(easy_handle, CURLOPT_URL, BACKEND_SERVER_URL);
    curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, req);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, proxy_curl_write_callback);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, req);
    curl_easy_setopt(easy_handle, CURLOPT_HEADERFUNCTION, proxy_curl_header_callback);
    curl_easy_setopt(easy_handle, CURLOPT_HEADERDATA, req);
    printf("http_handler: curl_easy_setopt 设置完成\n"); // 添加日志

    curl_multi_add_handle(multi_handle, easy_handle);
    printf("http_handler: curl_multi_add_handle 完成\n"); // 添加日志


    int still_running;
    do {
        struct timeval timeout;
        int maxfd = -1;
        fd_set fdread, fdwrite, fdexcep;
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
        int rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

        switch (rc) {
            case -1:
                perror("select error");
                still_running = 0;
                break;
            case 0:
            default:
                curl_multi_perform(multi_handle, &still_running);
                break;
        }
        printf("http_handler: curl_multi_perform 循环中, still_running = %d\n", still_running); // 添加日志
    } while (still_running);
    printf("http_handler: curl_multi_perform 循环结束\n"); // 添加日志


    CURLMsg *msg;
    int messages_left;
    while ((msg = curl_multi_info_read(multi_handle, &messages_left))) {
        if (msg->data.result == CURLE_OK) { // 修改了判断条件，只判断 CURLE_OK
            CURL *easy_handle_done = msg->easy_handle;
            struct evhttp_request *client_req = NULL;
            curl_easy_getinfo(easy_handle_done, CURLINFO_PRIVATE, &client_req);
            long response_code;
            curl_easy_getinfo(easy_handle_done, CURLINFO_RESPONSE_CODE, &response_code);

            if (response_code != 200) {
                fprintf(stderr, "后端请求失败，curl 错误码: %d, HTTP 状态码: %ld\n", msg->data.result, response_code); // 使用 fprintf 输出到标准错误
                evhttp_send_error(client_req, HTTP_INTERNAL, "Backend Request Failed");
                printf("http_handler: evhttp_send_error (后端请求失败) 发送完成\n"); // 添加日志
            } else {
                printf("http_handler: 后端请求成功\n"); // 添加日志
                evhttp_send_reply_end(client_req);
                printf("http_handler: evhttp_send_reply_end 发送完成\n"); // 添加日志
            }

            curl_multi_remove_handle(multi_handle, easy_handle_done);
            curl_easy_cleanup(easy_handle_done);
            printf("http_handler: curl easy handler 清理完成\n"); // 添加日志
        }
    }
    printf("http_handler: curl_multi_info_read 循环结束\n"); // 添加日志

    curl_multi_cleanup(multi_handle);
    printf("http_handler 函数结束\n"); // 添加日志
}

size_t proxy_curl_write_callback(char *buffer, size_t size, size_t nmemb, void *userp) {
    struct evhttp_request *req = (struct evhttp_request *)userp;
    struct evbuffer *client_output_buffer = evhttp_request_get_output_buffer(req);
    if (!client_output_buffer) return 0; // 检查输出 buffer 是否有效
    size_t total_size = size * nmemb;
    evbuffer_add(client_output_buffer, buffer, total_size);
    return total_size;
}

size_t proxy_curl_header_callback(char *buffer, size_t size, size_t nmemb, void *userp) {
    struct evhttp_request *req = (struct evhttp_request *)userp;
    struct evbuffer *client_output_buffer = evhttp_request_get_output_buffer(req);
    if (!client_output_buffer) return 0; // 检查输出 buffer 是否有效 // Added check here too, though header callback buffer failure less critical

    size_t total_size = size * nmemb;
    char *header_line = strndup(buffer, total_size);
    if (header_line) {
        if (strncmp(header_line, "HTTP/", 5) != 0 &&
            strncmp(header_line, "Date:", 5) != 0 &&
            strncmp(header_line, "Server:", 7) != 0 &&
            strncmp(header_line, "Transfer-Encoding:", 18) != 0 &&
            strncmp(header_line, "Connection:", 11) != 0) {
            char *colon = strchr(header_line, ':');
            if (colon) {
                *colon = '\0';
                char *name = header_line;
                char *value = colon + 1;
                while (*value == ' ' || *value == '\t') value++;
                // 使用 evhttp_add_header 来添加头部
                evhttp_add_header(evhttp_request_get_output_headers(req), name, value);
            }
        }
        free(header_line);
    }
    return total_size;
}

void timeout_cb(evutil_socket_t fd, short event, void *arg);

int main() {
    struct event_base *base = event_base_new();
    if (!base) {
        fprintf(stderr, "创建 event_base 失败\n");
        return 1;
    }

    struct evhttp *httpd = evhttp_new(base);
    if (!httpd) {
        fprintf(stderr, "创建 evhttp 失败\n");
        event_base_free(base);
        return 1;
    }

    evhttp_set_gencb(httpd, http_handler, NULL);

    struct evconnlistener *listener;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr("0.0.0.0");
    sin.sin_port = htons(8080);

    // 调试： 打印绑定的地址和端口
    fprintf(stderr, "尝试绑定到地址: %s, 端口: %d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

    listener = evconnlistener_new_bind(base,
            NULL,
            NULL,
            LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
            -1,
            (struct sockaddr*)&sin,
            sizeof(sin));

    // **更严格地检查 listener 创建结果，并输出错误信息**
    if (!listener) {
        int err = evutil_socket_geterror(evconnlistener_get_fd(listener)); // 尝试获取套接字错误
        fprintf(stderr, "绑定端口失败!\n");
        fprintf(stderr, "错误代码: %d, 错误信息: %s\n", err, evutil_socket_error_to_string(err)); // 输出错误代码和信息
        perror("evconnlistener_new_bind"); // 使用 perror 输出更详细的错误信息
        evhttp_free(httpd);
        event_base_free(base);
        return 1;
    } else {
        fprintf(stderr, "监听器 listener 创建成功，开始监听端口 %d\n", 8080);
    }


    printf("代理服务器已启动，监听端口 %d\n", 8080);


    // **调试： 强制添加一个定时器事件，看是否能阻止 event_base_dispatch 立即返回**
    struct event *timeout_event;
    timeout_event = event_new(base, -1, EV_PERSIST, timeout_cb, NULL); // -1 表示不关联 fd，EV_PERSIST 表示持久事件
    if (timeout_event) {
        struct timeval tv;
        tv.tv_sec = 5; // 5 秒后触发
        tv.tv_usec = 0;
        evtimer_add(timeout_event, &tv);
        fprintf(stderr, "添加了 5 秒定时器事件\n");
    } else {
        fprintf(stderr, "添加定时器事件失败!\n"); // 如果创建定时器事件失败，也输出错误信息
    }


    event_base_dispatch(base); // 进入 event loop

    // **调试： 检查 event_base_dispatch 是否意外退出**
    fprintf(stderr, "event_base_dispatch 返回了 (意外退出事件循环)\n");


    evconnlistener_free(listener);
    evhttp_free(httpd);
    event_base_free(base);

    return 0;
}

// 临时定时器回调函数 (仅用于调试)
void timeout_cb(evutil_socket_t fd, short event, void *arg) {
    fprintf(stderr, "running...\n");
    // 这里什么也不做，定时器只是为了让 event_base_dispatch 有事可做，不立即退出
}