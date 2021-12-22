#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "def.h"
#include "conf.h"

int sock, connection_state;
pthread_t recv_msg_tid;
char recv_buf[RECV_BUF_SIZE];
int recv_buf_head, recv_buf_tail;

void* recv_msg(void* arg);
void send_pkt(struct data_pkt* pkt, int data_len);

int get_int(int* res);          // 从命令行读取一个数字，如果输入非法返回0
int stoi(char* str, int* res);  // 字符串转数字，转换失败返回0

void print_menu();      // 打印菜单
void print_invalid();   // 说明输入非法
void print_next();      // 开启新行


void connect_to_server();       // 连接到服务器
void disconnect();              // 断开连接
void send_gettime();            // 询问时间
void send_getname();            // 询问服务器名称
void send_getconnlist();        // 询问连接列表
void send_msg();                // 发送消息
void read_pkt();                // 从缓冲队列读取一个完整的packet
void analysis_pkt(struct data_pkt* pkt);    // 分析数据包

int main(){

    signal(SIGPIPE, SIG_IGN);

    // command loop
    print_menu();
    while (1) {
        int command;
        // input command
        if (!get_int(&command)) {
            print_invalid();
            continue;
        }

        // exit
        if (command == 7) {
            disconnect();
            break;
        }

        switch (command)
        {
        case 0: // menu
            print_menu();
            break;
        case 1:
            connect_to_server();
            print_next();
            break;
        case 2: // disconnect
            disconnect();
            print_next();
            break;
        case 3: // get server time
            send_gettime();
            print_next();
            break;
        case 4:
            send_getname();
            print_next();
            break;
        case 5:
            send_getconnlist();
            print_next();
            break;
        case 6:
            send_msg();
            print_next();
            break;
        default:
            print_invalid();
            break;
        }
    }

    pthread_join(recv_msg_tid, NULL);
    
    //关闭套接字
    close(sock);
    
    printf("bye.\n");
    return 0;
}

void connect_to_server() {
    if (connection_state == 1) {
        printf("Already connected to server %s:%d\n", SERV_IP, SERV_PORT);
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));       //每个字节都用0填充
    serv_addr.sin_family = AF_INET;                 //使用IPv4地址
    serv_addr.sin_addr.s_addr = inet_addr(SERV_IP); //具体的IP地址
    serv_addr.sin_port = htons(SERV_PORT);          //端口
    
    int connect_res = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (connect_res == -1) {
        close(sock);
        perror("connect");
        return;
    }

    if (pthread_create(&recv_msg_tid, NULL, recv_msg, NULL) < 0) {
        close(sock);
        perror("pthread_create");
        return;
    }

    connection_state = 1;
    printf("Successfully connect to server %s:%d\n", SERV_IP, SERV_PORT);
}

void disconnect() {
    if (connection_state == 0) {
        printf("No connection.\n");
        return;
    }

    shutdown(sock, SHUT_RD);
    pthread_join(recv_msg_tid, NULL);
    recv_msg_tid = 0;

    printf("Disconnected with server.\n");
}

void send_gettime() {
    struct data_pkt pkt;
    pkt.command = CMD_GETTIME;
    pkt.str = NULL;
    for (int i = 0; i < 100; i++)
        send_pkt(&pkt, 0);
}

void send_getname() {
    struct data_pkt pkt;
    pkt.command = CMD_GETNAME;
    pkt.str = NULL;
    send_pkt(&pkt, 0);
}

void send_getconnlist() {
    struct data_pkt pkt;
    pkt.command = CMD_CONNLIST;
    pkt.str = NULL;
    send_pkt(&pkt, 0);
}

void send_msg() {
    int dest = -1;
    printf("To whom? ");
    fflush(stdout);

    if (!get_int(&dest)) {
        printf("Invalid number\n");
        return;
    }
    if (dest > 0xfe || dest < 0) {
        printf("Invalid number\n");
        return;
    }

    printf("Message: ");
    fflush(stdout);

    char data[128] = "";
    data[0] = dest;
    char *msg_str = data + 1;
    fgets(msg_str, 127, stdin);
    msg_str[strlen(msg_str) - 1] = '\0';

    struct data_pkt pkt;
    pkt.command = CMD_SEND;
    pkt.str = data;

    send_pkt(&pkt, strlen(msg_str) + 1);
}

void send_pkt(struct data_pkt* pkt, int data_len) {
    if (connection_state == 0) {
        puts("No connection");
        return;
    }

    char send_str[512];

    send_str[0] = '{';
    send_str[1] = pkt->command;

    int send_str_len = 2;
    if (pkt->str != NULL) {
        memcpy(send_str + 2, pkt->str, data_len);
        send_str_len += data_len;
    }

    send_str[send_str_len++] = '}';
    send_str[send_str_len] = '\0';

    send(sock, send_str, send_str_len, 0);
}

void* recv_msg(void* arg) {
    while (1) {
        char buf[1024] = "";

        ssize_t recv_res = recv(sock, buf, sizeof(buf) - 1, 0);
        if (recv_res <= 0) {
            connection_state = 0;
            recv_buf_head = recv_buf_tail = 0;
            close(sock);
            break;
        }
#ifdef DEBUG
        puts("");
        printf("recv_res: %ld\n", recv_res);
        printf("recv_msg: %s\n", buf);
        print_next();
#endif
        // 拷贝到缓冲区
        int data_len = recv_res;
        int finish_pkt = 0;
        for (int i = 0; i < data_len; i++) {
#ifdef DEBUG
            printf("240: i = %d, tail = %d\n", i, recv_buf_tail);
#endif
            recv_buf[recv_buf_tail] = buf[i];
            recv_buf_tail = RECV_NEXT(recv_buf_tail);
            // 每读到一个'}'，表示一个packet结束
            if (buf[i] == '}') {
                finish_pkt++;
            }
        }
        // 处理本轮读完的packet
        while (finish_pkt--) read_pkt();
    }

    printf("Connection lost.\n");
    print_next();
}

void read_pkt() {
#ifdef DEBUG
    printf("Debug: reading packet.\n");
    int _s = recv_buf_head, _e = recv_buf_tail;
    printf("Debug: head = %d, tail = %d\n", _s, _e);
    for (int i = _s; i != _e; i = RECV_NEXT(i)) {
        putchar(recv_buf[i]);
    }
    puts("");
    for (int i = _s; i != _e; i = RECV_NEXT(i)) {
        printf("%d ", recv_buf[i]);
    }
    puts("");
#endif

    struct data_pkt pkt;

        // 1. 找到packet头
    int head = -1, tail = -1;
    while (recv_buf_head != recv_buf_tail) {
        if (recv_buf[recv_buf_head] == '{') {
            head = recv_buf_head;
            break;
        }
        recv_buf_head++;
        recv_buf_head = RECV_NEXT(recv_buf_head);
    }
    if (head == -1) return; // 找不到头

#ifdef DEBUG
    printf("Debug: find head at %d\n", head);
#endif

    // 2. 找到packet尾
    for (int i = head; i != recv_buf_tail; i = RECV_NEXT(i)) {
        if (recv_buf[i] == '}') {
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
    pkt.command = recv_buf[head];
    head = RECV_NEXT(head);             // 跳过 command
    
    int data_len = tail - head - 1;     // -1 去掉末尾的'}'
    if (data_len < 0) data_len += RECV_BUF_SIZE;
    if (data_len != 0) {
        pkt.str = malloc(data_len + 1);
        for (int i = head, j = 0; j < data_len; i = RECV_NEXT(i), j++) {
            pkt.str[j] = recv_buf[i];
        }
        pkt.str[data_len] = '\0';
    } else pkt.str = NULL;

    // 4. 处理packet
    analysis_pkt(&pkt);
    free(pkt.str); // 释放内存

    // 5. 更新clnts
    recv_buf_head = tail;
}

int serv_times = 0;
void analysis_pkt(struct data_pkt* pkt) {
#ifdef DEBUG
    printf("Debug: command = %d\n", pkt->command);
    printf("Debug: message = %s\n", pkt->str);
#endif
    switch (pkt->command)
    {
    case RET_TIME:
        printf("Server time[%d]: %s\n", ++serv_times, pkt->str);
        break;
    case RET_NAME:
        printf("Server name: %s\n", pkt->str);
        break;
    case RET_CONNLIST:
        printf("Connection list:\n%s\n", pkt->str);
        break;
    case RET_SEND:
        printf("%s\n", pkt->str);
        break;
    case RET_BADCMD:
        printf("%s\n", pkt->str);
        break;
    case SERV_FORWARD:
        printf("Received message from client %d\n", pkt->str[0]);
        printf("{%s}\n", pkt->str + 1);
        break;
    default:
        printf("Unknown message\n");
        break;
    }

    print_next();
}

void print_menu() {
    puts("");
    puts("Input a single number to select.");
    puts("[1] Connect.");
    puts("[2] Disconnect.");
    puts("[3] Get server time.");
    puts("[4] Get server name.");
    puts("[5] Get active connection list.");
    puts("[6] Send a message.");
    puts("[7] Exit.");
    puts("[0] Display this menu again.");
    printf("--> ");
    fflush(stdout);
}

void print_invalid() {
    puts("");
    puts("Invalid command.");
    puts("Input \'0\' to show menu.");
    printf("--> ");
    fflush(stdout);
}

void print_next() {
    printf("--> ");
    fflush(stdout);
}

int stoi(char* str, int* res) {
    int len = strlen(str);
    *res = 0;

    if (str[len - 1] == '\n') {
        str[--len] = '\0';
    }

    if (len == 0) return 0;
    for (int i = 0; i < len; i++) {
        if (!isdigit(str[i])) {
            *res = 0;
            return 0;
        }
        *res = *res * 10 + str[i] - '0';
    }

    return 1;
}

int get_int(int* res) {
    char buf[128] = "";
    fgets(buf, sizeof(buf), stdin);

    return stoi(buf, res);
}