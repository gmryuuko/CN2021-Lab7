#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "def.h"
#include "conf.h"

char hello_str[] = "hello.";

// 服务器socket
int serv_sock = 0;
struct sockaddr_in serv_addr;

int clnt_cnt = 0;
struct clnt_struct {
    int valid;      // 是否在连接中
    pthread_t tid;  // 负责该client的子线程
    int clnt_sock;  // 该client对应的socket
    struct sockaddr_in clnt_addr;   // client地址
    char recv_buf[RECV_BUF_SIZE];   // client的消息缓冲队列
    int recv_buf_head, recv_buf_tail;
} clnts[MAX_CLIENT_CNT];

// 捕获ctrl + c，退出前断开所有连接
void terminate(int arg) {
    for (int i = 0; i < MAX_CLIENT_CNT; i++)
        if (clnts[i].valid) close(clnts[i].clnt_sock);
    
    close(serv_sock);
    puts("Bye.");
    exit(0);
}

// 增加一个cient连接
int add_clnt(int clnt_sock, struct sockaddr_in* clnt_addr);
// 每一个client连接单独开一个线程循环recv
void* msg_loop(void* arg);
// 分析data_pkt，并作出响应
void analysis_pkt(int from, struct data_pkt* pkt);
// 发送data_pkt
void send_pkt(int to, struct data_pkt* pkt, int data_len);
// 从clnt_struct的缓冲区读取一个packet
void read_pkt(int id);

int main(int argc, char** argv) {

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, terminate);

    // initialize server socket
    memset(&serv_addr, 0, sizeof(serv_addr));
    // server socket
    serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htons(INADDR_ANY);
    serv_addr.sin_port = htons(SERV_PORT);
    // bind
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    // listen socket
    listen(serv_sock, 20);
    printf("Listening at port %d\n\n", SERV_PORT);

    // connect with client
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size = sizeof(clnt_addr);
    
    // 循环等待client连接
    while (1) {
        int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        printf("Connecting with: %s:%d\n", inet_ntoa(clnt_addr.sin_addr), clnt_addr.sin_port);
        
        int id = add_clnt(clnt_sock, &clnt_addr);
        if (id == -1)
            printf("Failed to connect with %s:%d\n", inet_ntoa(clnt_addr.sin_addr), clnt_addr.sin_port);
        else printf("Successfully connected. user id: %d\n", id);
    }

    return 0;
}

int add_clnt(int clnt_sock, struct sockaddr_in* clnt_addr) {
    int id = 0;
    for (id = 0; id < MAX_CLIENT_CNT; id++)
        if (clnts[id].valid == 0)
            break;

    // 不能增加更多连接了
    if (id == MAX_CLIENT_CNT) {
        char fail_str[] = "server is full.";
        send(clnt_sock, fail_str, sizeof(fail_str), 0);
        close(clnt_sock);
        return -1;
    }

    clnts[id].valid = 1;
    clnts[id].clnt_sock = clnt_sock;
    clnts[id].clnt_addr = *clnt_addr;

    // 创建子线程处理该client
    int* arg = (int*)malloc(sizeof(int));
    *arg = id;
    pthread_create(&clnts[id].tid, NULL, msg_loop, (void*)arg);

    return id;
}

void* msg_loop(void* arg) {
    int id = *(int*)arg;
    free((int*)arg);

    while (1) {
        char recv_buf[1024] = "";

        ssize_t recv_res = recv(clnts[id].clnt_sock, recv_buf, sizeof(recv_buf) - 1, 0);
        // 连接断开
        if (recv_res <= 0) {
            printf("client %d offline.\n", id);
            close(clnts[id].clnt_sock);
            memset(clnts + id, 0, sizeof(struct clnt_struct));
            break;
        }
#ifdef DEBUG
        printf("recv res: %ld\n", recv_res);
        printf("recv msg from %d: %s\n", id, recv_buf);
#endif
        // 拷贝到该客户端对应的缓冲区
        int data_len = recv_res;
        int finish_pkt = 0;
        for (int i = 0; i < data_len; i++) {
            clnts[id].recv_buf[clnts[id].recv_buf_tail] = recv_buf[i];
            clnts[id].recv_buf_tail = RECV_NEXT(clnts[id].recv_buf_tail); // 取模
            // 每读到一个'}'，表示一个packet结束
            if (recv_buf[i] == '}') {
                finish_pkt++;
            }
        }
        
        // 处理本轮读完的packet
        while (finish_pkt--) read_pkt(id);
    }

}

void read_pkt(int id) {
#ifdef DEBUG
    printf("Debug: reading packet of client %d\n", id);
    int _s = clnts[id].recv_buf_head, _e = clnts[id].recv_buf_tail;
    printf("Debug: head = %d, tail = %d\n", _s, _e);
    for (int i = _s; i != _e; i = RECV_NEXT(i)) {
        putchar(clnts[id].recv_buf[i]);
    }
    puts("");
    for (int i = _s; i != _e; i = RECV_NEXT(i)) {
        printf("%d ", clnts[id].recv_buf[i]);
    }
    puts("");
#endif

    struct clnt_struct* cl = &clnts[id];
    struct data_pkt pkt;

    // 1. 找到packet头
    int head = -1, tail = -1;
    while (cl->recv_buf_head != cl->recv_buf_tail) {
        if (cl->recv_buf[cl->recv_buf_head] == '{') {
            head = cl->recv_buf_head;
            break;
        }
        cl->recv_buf_head++;
        cl->recv_buf_head = RECV_NEXT(cl->recv_buf_head);
    }
    if (head == -1) return; // 找不到头

#ifdef DEBUG
    printf("Debug: find head at %d\n", head);
#endif

    // 2. 找到packet尾
    for (int i = head; i != cl->recv_buf_tail; i = RECV_NEXT(i)) {
        if (cl->recv_buf[i] == '}') {
            tail = RECV_NEXT(i);
            break;
        }
    }
    if (tail == -1) return; // 找不到尾

#ifdef DEBUG
    printf("Debug: find tail at %d\n", tail);
#endif

    // 3. 构造packet
    head = RECV_NEXT(head);             // 跳过 '{'
    pkt.command = cl->recv_buf[head];
    head = RECV_NEXT(head);             // 跳过 command
    
    int data_len = tail - head - 1;     // -1 去掉末尾的'}'
    if (data_len < 0) data_len += RECV_BUF_SIZE;
    if (data_len != 0) {
        pkt.str = malloc(data_len);
        for (int i = head, j = 0; j < data_len; i = RECV_NEXT(i), j++) {
            pkt.str[j] = cl->recv_buf[i];
        }
    } else pkt.str = NULL;

    // 4. 处理packet
    analysis_pkt(id, &pkt);
    free(pkt.str); // 释放内存

    // 5. 更新clnts
    cl->recv_buf_head = tail;
}

void analysis_pkt(int from, struct data_pkt* pkt) {
#ifdef DEBUG
    printf("Analysis packet:\n");
    printf("command = %x\n", pkt->command);
    printf("str     = %s\n", (pkt->str == NULL ? "" : pkt->str));
#endif

    struct data_pkt res;
    char send_buf[512] = "";

    switch (pkt->command)
    {
    case CMD_GETTIME: {
        time_t cur_time;
        time(&cur_time);
        sprintf(send_buf, "%s", ctime(&cur_time));
        // 删掉最后一个换行符
        send_buf[strlen(send_buf) - 1] = '\0';
#ifdef DEBUG
        printf("current time: %s\n", ctime(&cur_time));
#endif
        res.command = RET_TIME;
        res.str = send_buf;
#ifdef DEBUG
        printf("res.str: %s\n", res.str);
#endif
        send_pkt(from, &res, strlen(send_buf));
	    printf("Send time to %d.\n", from);
        break;
    }

    case CMD_GETNAME: {
        gethostname(send_buf, sizeof(send_buf));
        res.command = RET_NAME;
        res.str = send_buf;
        send_pkt(from, &res, strlen(send_buf));
	    printf("Send name to %d.\n", from);
        break;
    }
    
    case CMD_CONNLIST: {
        int end = 0;
        end += sprintf(send_buf + end, "id\tip:port\n");
        for (int i = 0; i < MAX_CLIENT_CNT; i++)
            if (clnts[i].valid) {
                if (from == i)
                    end += sprintf(send_buf + end, "%d(you)\t%s:%d\n", i, inet_ntoa(clnts[i].clnt_addr.sin_addr), clnts[i].clnt_addr.sin_port);
                else
                    end += sprintf(send_buf + end, "%d\t%s:%d\n", i, inet_ntoa(clnts[i].clnt_addr.sin_addr), clnts[i].clnt_addr.sin_port);
            }
        res.command = RET_CONNLIST;
        res.str = send_buf;
        send_pkt(from, &res, strlen(send_buf));
	    printf("Send connection list to %d.\n", from);
        break;
    }

    case CMD_SEND: {
        char dest = pkt->str[0];
        if (dest < 0 || dest >= MAX_CLIENT_CNT || clnts[dest].valid == 0) {
            // 客户端不在线
            sprintf(send_buf, "Client %d is offline.\n", dest);
            res.command = RET_SEND;
            res.str = send_buf;
            send_pkt(from, &res, strlen(send_buf));
	        printf("Failed to forward a message. Client offline\n");
        } else {
            // 可以转发
            int data_len = 1 + strlen(pkt->str + 1);
            send_buf[0] = from;
            memcpy(send_buf + 1, pkt->str + 1, data_len - 1);

            // 发送给接收端
            struct data_pkt forward;
            forward.command = SERV_FORWARD;
            forward.str = send_buf;
            send_pkt(dest, &forward, data_len);

            // 返回给发送端
            sprintf(send_buf, "Successfully send a message to %d.\n", dest);
            res.command = RET_SEND;
            res.str = send_buf;
            send_pkt(from, &res, strlen(send_buf));
	        printf("Forward a message from %d to %d.\n", from, dest);
        }
        break;
    }
        
    default:
        break;
    }
}

void send_pkt(int to, struct data_pkt* pkt, int data_len) {
    if (clnts[to].valid == 0) {
        puts("Client %d is offline. Failed to send.");
        return;
    }

    char send_str[512];

    // 把 struct data_pkt 封装到字节流中
    send_str[0] = '{';
    send_str[1] = pkt->command;
    int send_str_len = 2;
    if (data_len != 0) {
        memcpy(send_str + 2, pkt->str, data_len);
        send_str_len += data_len;
    }

    send_str[send_str_len++] = '}';
    send_str[send_str_len] = '\0';

#ifdef DEBUG
    printf("send_str = %s\n", send_str);
#endif

    ssize_t send_res = send(clnts[to].clnt_sock, send_str, send_str_len, 0);

    if (send_res < 0) {
        shutdown(clnts[to].clnt_sock, SHUT_RD);
        return;
    }
}
